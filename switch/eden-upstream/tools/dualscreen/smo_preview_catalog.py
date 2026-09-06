"""Native costume assembly for the fixed Wait portrait (no gameplay mutations).

Base 0x444028: body/subactors, head resource suffix and optional body hair.
Base 0x446dcc: closet head/CapHair/Hair visibility and body-specific head fix.
All transforms below come from the selected body's InitPartsFixInfo BYML.
"""


def attachment(slot, model, fix, hidden=()):
    return {'slot': slot, 'model': model,
            'joint': 'Armature_' + fix['JointName'] if fix.get('JointName') else None,
            'rotation': [fix.get('LocalRotate', {}).get(k, 0) for k in 'XYZ'],
            'translation': [fix.get('LocalTrans', {}).get(k, 0) for k in 'XYZ'],
            'scale': [fix.get('LocalScale', {}).get(k, 1) if fix.get('UseLocalScale') else 1 for k in 'XYZ'],
            'hidden': list(hidden)}


def appearance(native, body, hat):
    info = native[body]['metadata']
    flags = info.get('InitCostumeInfoBody.byml') or {}
    # The native constructor substitutes Mario's resource for Invisible Cap.
    resource_hat = 'Mario' if hat == 'MarioInvisible' else hat
    suffix = ''
    if resource_hat.startswith('Mario64'):
        suffix = '' if body == 'Mario64' else 'Metal' if body == 'Mario64Metal' else 'Other'
    elif flags.get('IsUseShortHead') and resource_hat == 'MarioPeach':
        suffix = 'Short'
    head = resource_hat + 'Head' + suffix
    head_flags = native[head]['metadata'].get('InitCostumeInfoHead.byml') or {}
    sub = info.get('InitSubActorPauseMenu.byml') or info.get('InitSubActor.byml') or {}
    parts = []
    for part in sub.get('CreatorList', []):
        if part.get('ClassName') != 'PartsModel' or part['FixFileSuffixName'] == 'Head':
            continue
        slot = part['FixFileSuffixName']
        fix = info['InitPartsFixInfo' + slot + '.byml']
        parts.append(attachment(slot, part['ModelName'], fix))
    fix = info['InitPartsFixInfoHead.byml']
    if flags.get('IsUseHeadSuffix') and body != hat:
        fix = info.get('InitPartsFixInfoHead' + hat + '.byml', fix)
    hide_head_hair = flags.get('IsHideHeadHair', False) and not (
        flags.get('IsMario64') and resource_hat.startswith('Mario64'))
    parts.append(attachment('Head', head, fix, ['Hair', 'CapHair'] if hide_head_hair else ['Hair']))
    if flags.get('IsUseBodyHair') and not (flags.get('IsMario64') and head_flags.get('IsMario64')):
        no_cap = flags.get('IsExistHairNoCap') and head_flags.get('IsEnableHairNoCap')
        hair = body + 'Hair' + ('NoCap' if no_cap else '')
        parts.append(attachment('Hair', hair, info['InitPartsFixInfoHair.byml'],
                                ['Hair'] if not flags.get('IsMario64') else []))
    return {'model': body, 'parts': parts}


def build_appearances(native, catalog, available):
    result = {}
    for body in catalog['bodies']:
        result[body] = {}
        for hat in catalog['heads']:
            recipe = appearance(native, body, hat)
            missing = {recipe['model'], *(p['model'] for p in recipe['parts'])} - available
            if missing:
                raise ValueError(f'Incomplete preview {body}/{hat}: {sorted(missing)}')
            result[body][hat] = recipe
    return result
