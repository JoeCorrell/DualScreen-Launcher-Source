#include "Ui.h"
#include "UiVisualState.h"
#include "MapCatalog.h"
#include "MapObjectives.h"
#include "ItemCatalog.h"
#include "PortraitCatalog.h"
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <map>

namespace dishonored {
namespace {
using dis::Box;
using dis::Color;
constexpr Color Ground(12, 18, 20), Surface(22, 30, 32), Brass(115, 111, 90), Pale(172, 208, 211);
constexpr Color Cream(230, 224, 206), Muted(152, 165, 164), Ink(31, 29, 25), Red(163, 57, 51);
constexpr Box Content(32, 120, 1176, 816), MapBox(32, 120, 1176, 744);
constexpr wchar_t const* Titles[] = {L"MAP", L"POWERS", L"EQUIPMENT", L"OBJECTIVES", L"JOURNAL", L"BONE CHARMS", L"STATISTICS"};
constexpr wchar_t const* JournalFilters[] = {L"ALL", L"NOTES", L"BOOKS", L"AUDIOGRAPHS", L"CLUES", L"KEYS"};
constexpr wchar_t const* EquipmentFilters[] = {L"ALL", L"WEAPONS", L"AMMUNITION", L"GADGETS", L"SUPPLIES", L"UPGRADES"};
uint32_t EquipmentGroup(bridge::EquipmentKind kind) noexcept {
    switch (kind) {
        case bridge::EquipmentKind::Weapon: return 1;
        case bridge::EquipmentKind::Ammunition: return 2;
        case bridge::EquipmentKind::Gadget: return 3;
        case bridge::EquipmentKind::Consumable:
        case bridge::EquipmentKind::Resource: return 4;
        case bridge::EquipmentKind::Upgrade: return 5;
        default: return 0;
    }
}
uint32_t JournalGroup(bridge::JournalKind kind) noexcept {
    switch (kind) {
        case bridge::JournalKind::Book: return 2;
        case bridge::JournalKind::Audiograph: return 3;
        case bridge::JournalKind::Clue: return 4;
        case bridge::JournalKind::Key: return 5;
        default: return 1;
    }
}
constexpr uint64_t Background = 1, FrameIdle = 10, FrameSelected = 13, OccultRing = 16;
constexpr bridge::Key ChapterKey = UINT64_MAX;
std::wstring Quantity(bridge::Integer current, bridge::Integer maximum = {}) {
    std::wstring text = current.known ? std::to_wstring(current.value) : L"—";
    if (maximum.known) text += L" / " + std::to_wstring(maximum.value);
    return text;
}
bool Yes(bridge::Value<bool> value) { return value.known && value.value; }
}

struct Ui::Resources {
    ULONG_PTR gdiplus{};
    std::map<uint64_t, std::unique_ptr<Gdiplus::Image>> images;
    std::map<std::pair<int, bool>, HFONT> fonts;
    std::vector<std::wstring> fontFiles;
    uint64_t pixels{};
    ~Resources() {
        images.clear();
        for (auto& font : fonts) DeleteObject(font.second);
        for (auto& path : fontFiles) RemoveFontResourceExW(path.c_str(), FR_PRIVATE, nullptr);
        if (gdiplus) Gdiplus::GdiplusShutdown(gdiplus);
    }
};
Ui::Ui(Bridge& bridge) : bridge_(bridge), resources_(new Resources) { rows_.reserve(1024); hits_.reserve(1050); }
Ui::~Ui() = default;
bool Ui::Create(const std::wstring& directory) {
    if (!canvas_.Create(bridge::CanvasWidth, bridge::CanvasHeight)) { failure_ = L"1240x1080 canvas allocation failed"; return false; }
    if (!previousPage_.Create(1240, 1080) || !composed_.Create(1240, 1080)) { failure_ = L"Page transition canvas allocation failed"; return false; }
    Gdiplus::GdiplusStartupInput startup;
    if (Gdiplus::GdiplusStartup(&resources_->gdiplus, &startup, nullptr) != Gdiplus::Ok) { failure_ = L"GDI+ initialization failed"; return false; }
    for (const auto* file : {L"body.ttf", L"title.ttf"}) {
        std::wstring path = directory + L"\\" + file;
        if (!AddFontResourceExW(path.c_str(), FR_PRIVATE, nullptr)) { failure_ = L"Original font unavailable: " + path; return false; }
        resources_->fontFiles.push_back(path);
    }
    for (const auto& map : MapAssets)
        if (!RegisterImage(2000 + map.native, directory + L"\\" + map.file)) return false;
    uint64_t itemId = 1000;
    for (const auto& item : ItemAssets)
        if (!RegisterImage(itemId++, directory + L"\\" + item.file)) return false;
    uint64_t portraitId = 3000;
    for (const auto& portrait : PortraitAssets)
        if (!RegisterImage(portraitId++, directory + L"\\" + portrait.file)) return false;
    // Missing original resources are observable initialization failures, never
    // silently replaced by a screenshot or made-up game artwork.
    return resources_->fontFiles.size() == 2 && RegisterImage(Background, directory + L"\\journal-background.png") &&
        RegisterImage(FrameIdle, directory + L"\\frame-idle-a.png") &&
        RegisterImage(FrameIdle + 1, directory + L"\\frame-idle-b.png") &&
        RegisterImage(FrameIdle + 2, directory + L"\\frame-idle-c.png") &&
        RegisterImage(FrameSelected, directory + L"\\frame-selected-a.png") &&
        RegisterImage(FrameSelected + 1, directory + L"\\frame-selected-b.png") &&
        RegisterImage(FrameSelected + 2, directory + L"\\frame-selected-c.png") &&
        RegisterImage(OccultRing, directory + L"\\occult-ring.png") &&
        RegisterImage(102, directory + L"\\power-blink.png") &&
        RegisterImage(103, directory + L"\\power-bend-time.png") &&
        RegisterImage(104, directory + L"\\power-windblast.png") &&
        RegisterImage(105, directory + L"\\power-possession.png") &&
        RegisterImage(106, directory + L"\\power-dark-vision.png") &&
        RegisterImage(107, directory + L"\\power-devouring-swarm.png") &&
        RegisterImage(108, directory + L"\\power-vitality.png") &&
        RegisterImage(109, directory + L"\\power-agility.png") &&
        RegisterImage(110, directory + L"\\power-blood-thirsty.png") &&
        RegisterImage(111, directory + L"\\power-shadow-kill.png") &&
        RegisterImage(4, directory + L"\\coin.png");
}
bool Ui::RegisterImage(uint64_t id, const std::wstring& path) {
    if (!resources_->gdiplus || !id || resources_->images.count(id) || resources_->images.size() >= 256) {
        failure_ = L"Invalid or duplicate resource registration: " + path; return false;
    }
    std::unique_ptr<Gdiplus::Image> image(Gdiplus::Image::FromFile(path.c_str(), FALSE));
    if (!image || image->GetLastStatus() != Gdiplus::Ok) { failure_ = L"Original image could not be decoded: " + path; return false; }
    const uint64_t pixels = uint64_t(image->GetWidth()) * image->GetHeight();
    if (!pixels || image->GetWidth() > 8192 || image->GetHeight() > 8192 ||
        pixels + resources_->pixels > 32 * 1024 * 1024) { failure_ = L"Image exceeds the resource budget: " + path; return false; }
    if (id >= FrameIdle && id < FrameSelected + 3 && (image->GetWidth() != 184 || image->GetHeight() != 148)) {
        failure_ = L"Frame dimensions differ from the original export: " + path; return false;
    }
    resources_->pixels += pixels; resources_->images.emplace(id, std::move(image)); dirty_ = true; backdropReady_ = false; return true;
}
std::wstring_view Ui::Text(bridge::TextRef ref) const noexcept {
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    if (!snapshot_ || !bridge::ValidText(*snapshot_, ref)) return {};
    return {reinterpret_cast<const wchar_t*>(snapshot_->text.data() + ref.offset), ref.length};
}
void Ui::Label(Box box, std::wstring_view text, int size, bool title, Color color, bool wrap, dis::Align align) {
    if (text.empty() || text.size() > bridge::TextCapacity) return;
    const auto fontFor = [&](int pixels) {
        const auto key = std::make_pair(pixels, title);
        auto found = resources_->fonts.find(key);
        if (found == resources_->fonts.end()) {
            HFONT font = CreateFontW(-pixels, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
                title ? L"Emerge BF" : L"ChaletComprime-CologneEighty");
            found = resources_->fonts.emplace(key, font).first;
        }
        return found->second;
    };
    HDC dc = canvas_.dc(); HGDIOBJ previous = SelectObject(dc, fontFor(size));
    if (box.w > 0 && box.h > 0) {
        // Fit actual font metrics, including wrapped titles, inside their
        // reserved region. Never scale bitmap text or intrude on status rows.
        for (;;) {
            RECT measured{0, 0, box.w, 0};
            DrawTextW(dc, text.data(), int(text.size()), &measured,
                DT_NOPREFIX | DT_CALCRECT | (wrap ? DT_WORDBREAK : DT_SINGLELINE));
            if ((measured.right <= box.w && measured.bottom <= box.h) || size <= 18) break;
            size -= 2; SelectObject(dc, fontFor(size));
        }
    }
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(color.r, color.g, color.b));
    RECT rectangle{box.x, box.y, box.right(), box.bottom()};
    DrawTextW(dc, text.data(), int(text.size()), &rectangle,
        DT_NOPREFIX | (align == dis::Align::kCenter ? DT_CENTER : align == dis::Align::kRight ? DT_RIGHT : 0) |
        (wrap ? DT_WORDBREAK : DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS));
    SelectObject(dc, previous);
}
bool Ui::Image(uint64_t id, Box box, float opacity) {
    auto found = resources_->images.find(id);
    if (found == resources_->images.end() || box.w <= 0 || box.h <= 0 || !std::isfinite(opacity) || opacity <= 0 || opacity > 1) return false;
    // Preserve the native proportions of illustrations and portraits. Only
    // the frame/background slices are intended to stretch across a surface.
    if (id >= 100 || id == OccultRing || id == 4) {
        const double scale = std::min(double(box.w) / found->second->GetWidth(), double(box.h) / found->second->GetHeight());
        const int width = std::max(1, int(found->second->GetWidth() * scale));
        const int height = std::max(1, int(found->second->GetHeight() * scale));
        box.x += (box.w - width) / 2; box.y += (box.h - height) / 2; box.w = width; box.h = height;
    }
    Gdiplus::Graphics graphics(canvas_.dc());
    RECT clip{}; GetClipBox(canvas_.dc(), &clip);
    graphics.SetClip(Gdiplus::Rect(clip.left, clip.top, clip.right - clip.left, clip.bottom - clip.top));
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
    if (opacity < 1) {
        Gdiplus::ColorMatrix matrix = {{{1, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 0, opacity, 0}, {0, 0, 0, 0, 1}}};
        Gdiplus::ImageAttributes attributes;
        attributes.SetColorMatrix(&matrix);
        return graphics.DrawImage(found->second.get(), Gdiplus::Rect(box.x, box.y, box.w, box.h),
            0, 0, int(found->second->GetWidth()), int(found->second->GetHeight()), Gdiplus::UnitPixel, &attributes) == Gdiplus::Ok;
    }
    return graphics.DrawImage(found->second.get(), box.x, box.y, box.w, box.h) == Gdiplus::Ok;
}
void Ui::Border(Box box, bool selected, uint64_t variant) {
    if (box.w < 4 || box.h < 4) return;
    // The exported menu frames contain 24px of shadow before their visible
    // edge. Nine-slicing them collapsed short buttons into slivers. Draw a
    // quiet etched edge at the actual control bounds, without glow or shadow.
    const Color edge = selected ? Pale : Brass.Alpha(145);
    const int x = box.x + 1, y = box.y + 1, right = box.right() - 2, bottom = box.bottom() - 2;
    canvas_.Line(x, y, right, y, 1, edge);
    canvas_.Line(x, bottom, right, bottom, 1, edge);
    canvas_.Line(x, y, x, bottom, 1, edge);
    canvas_.Line(right, y, right, bottom, 1, edge);
    if (selected) canvas_.FillRect({box.x + 1, box.y + 8, 3, box.h - 16}, Pale);
    (void)variant;
}
void Ui::Button(Box box, std::wstring_view text, HitKind kind, bridge::Key key, uint32_t value, bool enabled) {
    bool active = (kind == HitKind::JournalFilter && value == journalFilter_) ||
        (kind == HitKind::EquipmentFilter && value == equipmentFilter_) ||
        (kind == HitKind::StatisticsFilter && value == uint32_t(savedStatistics_)) ||
        (kind == HitKind::MapSheet && snapshot_ && preferredMap_ == key) ||
        (kind == HitKind::MapSheets && mapSheetsOpen_) ||
        (kind == HitKind::MapLegend && mapLegendOpen_) ||
        IsPressed(kind, key, value);
    canvas_.FillRect(box, active ? Color(45, 62, 65) : Surface);
    Border(box, active, key ? key : value);
    Label(box.Inset(12, 14), text, 21, true, enabled ? Cream : Muted, false, dis::Align::kCenter);
    hits_.push_back({box, kind, key, value, enabled});
}
const bridge::Domain& Ui::Domain() const noexcept {
    switch (page_) {
        case bridge::Page::Map: return snapshot_->map.domain;
        case bridge::Page::Powers: return snapshot_->powers.domain;
        case bridge::Page::Equipment: return snapshot_->equipment.domain;
        case bridge::Page::Objectives: return snapshot_->objectives.domain;
        case bridge::Page::Journal: return snapshot_->journal.domain;
        case bridge::Page::BoneCharms: return snapshot_->charms.domain;
        default: return snapshot_->statistics.domain;
    }
}
void Ui::Update(const bridge::Snapshot& snapshot, uint64_t now) {
    const auto bootstrap = bridge_.bootstrap.load(std::memory_order_acquire);
    if (bootstrap_ != bootstrap) { bootstrap_ = bootstrap; dirty_ = true; }
    if (snapshot.version != bridge::Version || snapshot.size != sizeof(snapshot)) { snapshot_ = nullptr; Cancel(); dirty_ = true; return; }
    const bool fresh = bridge::Fresh(snapshot, now);
    const bool newSnapshot = sequence_ != snapshot.sequence;
    const auto visibleState = VisibleState(snapshot, page_, fresh, now);
    if (visibleState_ != visibleState) { visibleState_ = visibleState; dirty_ = true; }
    if (sequence_ != snapshot.sequence || (nextTargetExpiry_ && now >= nextTargetExpiry_)) {
        if ((page_ == bridge::Page::Objectives || page_ == bridge::Page::Map) && nextTargetExpiry_ && now >= nextTargetExpiry_) dirty_ = true;
        nextTargetExpiry_ = 0;
        for (uint32_t i = 0; i < std::min<uint32_t>(snapshot.objectiveTargets.domain.stored, snapshot.objectiveTargets.entries.size()); ++i) {
            const auto at = snapshot.objectiveTargets.entries[i].observedAt;
            if (at > now || now - at > bridge::FreshnessMilliseconds) continue;
            const auto expires = at + bridge::FreshnessMilliseconds + 1;
            if (!nextTargetExpiry_ || expires < nextTargetExpiry_) nextTargetExpiry_ = expires;
        }
    }
    if (generation_ != snapshot.generation) {
        Cancel(); selected_.fill(0); listScroll_ = {}; detailScroll_ = {}; pendingRequest_ = 0;
        generation_ = snapshot.generation;
        mapLegendOpen_ = mapSheetsOpen_ = false; mapKey_ = preferredMap_ = 0; mapView_ = {};
    }
    // Tick sampling stops while paused. Retain this pointer-free snapshot for
    // browsing; only an explicit lifecycle/generation change clears the view.
    if (snapshot.lifecycle != bridge::Lifecycle::Live) Cancel();
    if (fresh_ != fresh) dirty_ = true;
    sequence_ = snapshot.sequence; fresh_ = fresh; snapshot_ = &snapshot;
    // Rows retain pointers into a mailbox slot. Refresh these even when the
    // displayed values did not change, without rasterizing the whole screen.
    if (newSnapshot && !dirty_) BuildRows();
    // A health/ammo/objective update must not interrupt an active drag or
    // press. Release resolves the same semantic target in the fresh layout;
    // its old vector index is never reused to activate a different item.
    bridge::Acknowledgement acknowledgement{};
    while (bridge_.acknowledgements.Pop(acknowledgement)) {
        if (acknowledgement.generation != generation_ || acknowledgement.request != pendingRequest_) continue;
        pendingRequest_ = 0;
        const bool audio = pendingKind_ == bridge::CommandKind::PlayAudiograph || pendingKind_ == bridge::CommandKind::StopAudiograph;
        feedback_ = audio ? (acknowledgement.result == bridge::CommandResult::Applied ?
            (pendingKind_ == bridge::CommandKind::PlayAudiograph ? L"Playback started" : L"Playback stopped") : L"Playback unavailable") :
            pendingKind_ == bridge::CommandKind::ReadDocument ?
            (acknowledgement.result == bridge::CommandResult::Applied ? L"Document opened" : L"Document unavailable") :
            (acknowledgement.result == bridge::CommandResult::Applied ? L"Selection confirmed" : L"Selection unavailable");
        feedbackUntil_ = now + 2200; dirty_ = true;
    }
    if (feedbackUntil_ && now >= feedbackUntil_) { feedbackUntil_ = 0; feedback_.clear(); dirty_ = true; }
}
void Ui::BuildRows() {
    rows_.clear();
    if (!snapshot_) return;
    switch (page_) {
        case bridge::Page::Powers:
            for (uint32_t i = 0; i < std::min<uint32_t>(snapshot_->powers.domain.stored, snapshot_->powers.entries.size()); ++i) {
                const auto& row = snapshot_->powers.entries[i];
                std::wstring annotation = !row.unlocked.known ? L"—" : Yes(row.unlocked) ? L"Tier " + Quantity(row.tier, row.maximumTier) : L"Locked";
                if (row.manaCost.known) annotation += L"   Mana " + Quantity(row.manaCost);
                else if (row.baseManaCost.known) annotation += L"   Base mana " + Quantity(row.baseManaCost);
                rows_.push_back({row.key, &row.label, annotation, Yes(row.selected), Yes(row.selectable), bridge::CommandKind::SelectPower});
            }
            break;
        case bridge::Page::Equipment:
            for (uint32_t i = 0; i < std::min<uint32_t>(snapshot_->equipment.domain.stored, snapshot_->equipment.entries.size()); ++i) {
                const auto& row = snapshot_->equipment.entries[i];
                if (equipmentFilter_ && EquipmentGroup(row.kind) != equipmentFilter_) continue;
                std::wstring annotation = Quantity(row.quantity, row.capacity);
                if (row.kind == bridge::EquipmentKind::Weapon || row.kind == bridge::EquipmentKind::Gadget)
                    annotation = Yes(row.selected) ? L"Equipped" : L"Owned";
                else if (row.kind == bridge::EquipmentKind::Ammunition && Yes(row.selected)) annotation = L"Equipped   " + annotation;
                else if (row.kind == bridge::EquipmentKind::Upgrade) annotation = L"Purchased";
                rows_.push_back({row.key, &row.label, annotation, Yes(row.selected), Yes(row.selectable),
                    row.kind == bridge::EquipmentKind::Consumable ? bridge::CommandKind::UseConsumable : bridge::CommandKind::SelectEquipment});
            }
            break;
        case bridge::Page::BoneCharms:
            for (uint32_t i = 0; i < std::min<uint32_t>(snapshot_->charms.domain.stored, snapshot_->charms.entries.size()); ++i) {
                const auto& row = snapshot_->charms.entries[i];
                bool equipped = Yes(row.equipped);
                rows_.push_back({row.key, &row.label, equipped ? L"Equipped" : L"Collected", equipped,
                    equipped ? Yes(row.canRemove) : Yes(row.canEquip), equipped ? bridge::CommandKind::RemoveCharm : bridge::CommandKind::EquipCharm});
            }
            break;
        case bridge::Page::Objectives:
            if (snapshot_->chapter.title.length)
                rows_.push_back({ChapterKey, &snapshot_->chapter, L"Current mission", false, false, {}});
            for (uint32_t i = 0; i < std::min<uint32_t>(snapshot_->objectives.domain.stored, snapshot_->objectives.entries.size()); ++i) {
                const auto& row = snapshot_->objectives.entries[i];
                auto note = row.state == bridge::ObjectiveState::Completed ? L"Completed" :
                    row.state == bridge::ObjectiveState::Failed ? L"Failed" : Yes(row.optional) ? L"Optional" : L"Primary";
                std::wstring annotation = note;
                if (row.status.length) { annotation += L"   "; annotation += Text(row.status); }
                rows_.push_back({row.key, &row.label, annotation, Yes(row.tracked), false, {}, row.parent != 0});
            }
            break;
        case bridge::Page::Journal:
            for (uint32_t i = 0; i < std::min<uint32_t>(snapshot_->journal.domain.stored, snapshot_->journal.entries.size()); ++i) {
                const auto& row = snapshot_->journal.entries[i];
                if (journalFilter_ && JournalGroup(row.kind) != journalFilter_) continue;
                const wchar_t* category = row.kind == bridge::JournalKind::Book ? L"Book" :
                    row.kind == bridge::JournalKind::Map ? L"Map" : row.kind == bridge::JournalKind::Audiograph ? L"Audiograph" :
                    row.kind == bridge::JournalKind::Key ? L"Key" : row.kind == bridge::JournalKind::Clue ? L"Clue" : L"Document";
                rows_.push_back({row.key, &row.label, Yes(row.playing) ? L"Playing" : category, Yes(row.playing), !row.contentsComplete, bridge::CommandKind::ReadDocument});
            }
            if (!journalFilter_ || journalFilter_ == 5) {
                for (uint32_t i = 0; i < std::min<uint32_t>(snapshot_->equipment.domain.stored, snapshot_->equipment.entries.size()); ++i) {
                    const auto& key = snapshot_->equipment.entries[i];
                    if (key.kind != bridge::EquipmentKind::Key) continue;
                    const auto name = Text(key.label.title);
                    if (std::any_of(rows_.begin(), rows_.end(), [&](const Row& row) { return row.annotation == L"Key" && Text(row.label->title) == name; })) continue;
                    rows_.push_back({key.key, &key.label, L"Key", false, false, {}});
                }
            }
            break;
        default: break;
    }
    auto& selected = selected_[uint32_t(page_)];
    if (selected && std::none_of(rows_.begin(), rows_.end(), [selected](const Row& row) { return row.key == selected; })) {
        selected = 0; detailScroll_[uint32_t(page_)] = {}; Cancel();
    }
}
void Ui::Frame(uint64_t now) {
    if (!backdropReady_ || !BitBlt(canvas_.dc(), 0, 0, 1240, 1080, backdrop_.dc(), 0, 0, SRCCOPY)) {
        canvas_.Clear(Ground);
        Image(Background, {0, 0, 1240, 1080});
        canvas_.FillRect({0, 0, 1240, 1080}, Ground.Alpha(130));
        Border({0, 0, 1240, 1080});
        Image(OccultRing, {1090, 18, 96, 80}, 0.45f);
        if ((backdrop_.ready() || backdrop_.Create(1240, 1080)) &&
            BitBlt(backdrop_.dc(), 0, 0, 1240, 1080, canvas_.dc(), 0, 0, SRCCOPY)) backdropReady_ = true;
    }
    Label({32, 26, 600, 60}, Titles[uint32_t(page_)], 42, true);
    for (uint32_t i = 0; i < 7; ++i) canvas_.FillCircle(40 + int(i) * 18, 99, 3, i == uint32_t(page_) ? Pale : Brass);
    canvas_.Line(32, 108, 1208, 108, 1, Brass);
    std::wstring status = feedback_;
    if (status.empty() && !fresh_) status = snapshot_ && snapshot_->lifecycle == bridge::Lifecycle::Live ?
        L"Browsing last game state" : L"Waiting for Corvo";

    Label({48, 1008, 720, 36}, status, 26, false, Muted);
    Label({792, 1008, 400, 36}, page_ == bridge::Page::Map ? L"SWIPE HEADER TO CHANGE PAGE" : L"SWIPE TO CHANGE PAGE", 23, false, Brass, false, dis::Align::kRight);
    if (snapshot_) {
        auto ratio = [](bridge::Integer value, bridge::Integer maximum) {
            return value.known && maximum.known && maximum.value > 0 ? float(value.value) / float(maximum.value) : -1.0f;
        };
        const auto vital = [&](int x, const wchar_t* title, bridge::Integer value, bridge::Integer maximum, Color colour) {
            Label({x, 948, 240, 28}, title, 22, true, Muted);
            Label({x + 264, 948, 288, 28}, Quantity(value, maximum), 26, false, Cream, false, dis::Align::kRight);
            canvas_.Bar({x, 980, 552, 12}, ratio(value, maximum), colour, Surface);
        };
        vital(48, L"HEALTH", snapshot_->health, snapshot_->maximumHealth, Red);
        vital(640, L"MANA", snapshot_->mana, snapshot_->maximumMana, Pale);
    }
    (void)now;
}
void Ui::Details(const Row& row, Box box, bool document) {
    const bool dock = page_ == bridge::Page::Powers || page_ == bridge::Page::Equipment || page_ == bridge::Page::BoneCharms;
    canvas_.FillRect(box, Color(192, 180, 151)); Border(box, false, row.key);
    Label({box.x + 40, box.y + 28, box.w - (dock ? 148 : 80), 58}, Text(row.label->title), 28, true, Ink, true);
    if (dock) Button({box.right() - 84, box.y + 20, 64, 64}, L"×", HitKind::CloseDetails);
    std::wstring_view description = Text(row.label->description);
    std::wstring objectiveDescription;
    if (page_ == bridge::Page::Objectives) {
        objectiveDescription = Text(row.label->title) == description ? std::wstring{} : std::wstring(description);
        if (!row.annotation.empty()) {
            if (!objectiveDescription.empty()) objectiveDescription += L"\n\n";
            objectiveDescription += row.annotation;
        }
        bool heading = false;
        for (uint32_t i = 0; i < std::min<uint32_t>(snapshot_->objectiveTargets.domain.stored, snapshot_->objectiveTargets.entries.size()); ++i) {
            const auto& target = snapshot_->objectiveTargets.entries[i];
            if (target.observedAt > frameAt_ || frameAt_ - target.observedAt > bridge::FreshnessMilliseconds) continue;
            bool belongs = target.task == row.key || row.key == ChapterKey;
            if (!belongs) {
                for (uint32_t j = 0; j < std::min<uint32_t>(snapshot_->objectives.domain.stored, snapshot_->objectives.entries.size()); ++j) {
                    const auto& task = snapshot_->objectives.entries[j];
                    if (task.key == target.task && task.parent == row.key) { belongs = true; break; }
                }
            }
            if (!belongs || !target.label.title.length) continue;
            if (!heading) { objectiveDescription += L"\n\nCURRENT TARGETS"; heading = true; }
            objectiveDescription += L"\n\n";
            objectiveDescription += Text(target.label.title);
            if (target.distanceMetres.known) {
                objectiveDescription += L"   "; objectiveDescription += Quantity(target.distanceMetres); objectiveDescription += L" m";
            }
            if (target.optional) objectiveDescription += L"\nOptional";
        }
        description = objectiveDescription;
    }
    const bridge::JournalEntry* audiograph = nullptr;
    if (document) {
        for (const auto& entry : snapshot_->journal.entries) if (entry.key && entry.key == row.key) {
            description = entry.contentsComplete ? Text(entry.contents) : L"Document contents are not yet available.";
            if (entry.kind == bridge::JournalKind::Audiograph) audiograph = &entry;
            break;
        }
    }
    auto& scroll = detailScroll_[uint32_t(page_)];
    int metadataHeight = 0;
    if (audiograph) {
        const bool playing = Yes(audiograph->playing);
        const auto command = playing ? bridge::CommandKind::StopAudiograph : bridge::CommandKind::PlayAudiograph;
        Button({box.x + 40, box.y + 100, 236, 70}, playing ? L"STOP" : L"PLAY", HitKind::Action,
            row.key, uint32_t(command), fresh_ && !pendingRequest_ && Yes(playing ? audiograph->canStop : audiograph->canPlay));
        Label({box.x + 294, box.y + 114, box.w - 334, 40}, playing ? L"Playing recording" : L"Audiograph", 25, false, Ink);
        metadataHeight = 80;
    }
    if (page_ == bridge::Page::Powers) {
        for (uint32_t i = 0; i < std::min<uint32_t>(snapshot_->powers.domain.stored, snapshot_->powers.entries.size()); ++i) {
            const auto& power = snapshot_->powers.entries[i];
            if (power.key != row.key) continue;
            std::wstring metadata = L"Runes held: " + Quantity(snapshot_->runes);
            if (power.runeRequirement.known) metadata += L"     Next tier: " + Quantity(power.runeRequirement) + L" runes";
            else if (power.tier.known && power.maximumTier.known && power.tier.value == power.maximumTier.value) metadata += L"     Fully upgraded";
            Label({box.x + 40, box.y + 100, box.w - 80, 34}, metadata, 27, false, Ink);
            metadataHeight = 40; break;
        }
    }
    if (page_ == bridge::Page::Equipment) {
        for (uint32_t i = 0; i < std::min<uint32_t>(snapshot_->equipment.domain.stored, snapshot_->equipment.entries.size()); ++i) {
            const auto& item = snapshot_->equipment.entries[i];
            if (item.key != row.key || (!item.damage.known && !item.headshotMultiplier.known)) continue;
            std::wstring metadata;
            wchar_t number[64]{};
            if (item.damage.known) {
                swprintf(number, 64, L"%.6g", double(item.damage.value));
                metadata = item.rangedDamage ? L"Ranged damage: " : L"Melee damage: "; metadata += number;
            }
            if (item.headshotMultiplier.known) {
                swprintf(number, 64, L"%.6g", double(item.headshotMultiplier.value));
                if (!metadata.empty()) metadata += L"     ";
                metadata += L"Headshot multiplier: "; metadata += number;
            }
            Label({box.x + 40, box.y + 100, box.w - 80, 34}, metadata, 27, false, Ink);
            Label({box.x + 40, box.y + 136, box.w - 80, 30}, L"Before attack and target modifiers", 24, false, Ink);
            metadataHeight = 76; break;
        }
    }
    const int actionWidth = row.actionable ? 260 : 0;
    Box reader{box.x + 40, box.y + 116 + metadataHeight, box.w - 80 - actionWidth, box.h - 144 - metadataHeight};
    detailBox_ = reader;
    // Measure with the same face/size/width used for drawing; full documents
    // remain scrollable instead of being ellipsized into a fixed-height box.
    Label({0, 0, 0, 0}, L" ", 26, true);
    HDC dc = canvas_.dc(); auto font = resources_->fonts.find({26, true});
    HGDIOBJ previous = SelectObject(dc, font->second);
    RECT measured{0, 0, reader.w, 0};
    DrawTextW(dc, description.data(), int(description.size()), &measured, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    SelectObject(dc, previous);
    scroll.maximum = float(std::max(0L, measured.bottom - reader.h));
    scroll.offset = std::clamp(scroll.offset, 0.0f, scroll.maximum);
    int saved = SaveDC(dc); IntersectClipRect(dc, reader.x, reader.y, reader.right(), reader.bottom());
    Label({reader.x, reader.y - int(scroll.offset), reader.w, std::max(reader.h, int(measured.bottom))}, description, 26, true, Ink, true);
    RestoreDC(dc, saved);
    ScrollIndicator({reader.x, reader.y, reader.w + 12, reader.h}, scroll);
    if (row.actionable) Button({box.right() - 276, box.bottom() - 104, 236, 70},
        row.action == bridge::CommandKind::RemoveCharm ? L"REMOVE" : row.action == bridge::CommandKind::UseConsumable ? L"USE ELIXIR" :
            row.action == bridge::CommandKind::ReadDocument ? L"READ" : row.action == bridge::CommandKind::EquipCharm ? L"EQUIP" : L"SELECT",
        HitKind::Action, row.key, uint32_t(row.action), fresh_ && !pendingRequest_);
}
void Ui::Cards(unsigned columns, int top, int height) {
    const int gap = 16, width = (Content.w - 16 - int(columns - 1) * gap) / int(columns);
    const bool compact = page_ == bridge::Page::Equipment || page_ == bridge::Page::BoneCharms;
    const int rowHeight = compact ? 224 : 260;
    auto& scroll = listScroll_[uint32_t(page_)];
    int rows = int((rows_.size() + columns - 1) / columns);
    scroll.maximum = float(std::max(0, rows * (rowHeight + gap) - gap - height));
    scroll.offset = std::clamp(scroll.offset, 0.0f, scroll.maximum);
    Box clip{Content.x, top, Content.w, height}; listBox_ = clip; canvas_.SetClip(clip);
    int saved = SaveDC(canvas_.dc()); IntersectClipRect(canvas_.dc(), clip.x, clip.y, clip.right(), clip.bottom());
    for (uint32_t i = 0; i < rows_.size(); ++i) {
        const Row& row = rows_[i];
        Box box{Content.x + int(i % columns) * (width + gap), top + int(i / columns) * (rowHeight + gap) - int(scroll.offset), width, rowHeight};
        if (box.bottom() <= clip.y || box.y >= clip.bottom()) continue;
        bool selected = selected_[uint32_t(page_)] == row.key ||
            IsPressed(HitKind::Entry, row.key);
        canvas_.FillRect(box, selected ? Color(36, 48, 50) : Surface);
        Border(box, selected || row.selectedInGame, row.key);
        const int iconSize = compact ? 88 : 116;
        const bool locked = page_ == bridge::Page::Powers && i < snapshot_->powers.domain.stored &&
            snapshot_->powers.entries[i].unlocked.known && !snapshot_->powers.entries[i].unlocked.value;
        Image(row.label->artwork.id, {box.x + (box.w - iconSize) / 2, box.y + 20, iconSize, iconSize}, locked ? 0.55f : 1.0f);
        Label({box.x + 28, box.bottom() - 110, box.w - 56, 60}, Text(row.label->title), compact ? 25 : 28, true, Cream, true, dis::Align::kCenter);
        Label({box.x + 28, box.bottom() - 44, box.w - 56, 28}, row.annotation, 25, false, row.selectedInGame ? Pale : Muted, false, dis::Align::kCenter);
        Box target{box.x, std::max(box.y, clip.y), box.w, std::min(box.bottom(), clip.bottom()) - std::max(box.y, clip.y)};
        if (target.h >= 64) hits_.push_back({target, HitKind::Entry, row.key, i, true});
    }
    RestoreDC(canvas_.dc(), saved); canvas_.ClearClip();
    ScrollIndicator(clip, scroll);
    auto found = std::find_if(rows_.begin(), rows_.end(), [this](const Row& row) { return row.key == selected_[uint32_t(page_)]; });
    if (found != rows_.end()) Details(*found, {Content.x, top + height + 16, Content.w, Content.bottom() - top - height - 16});
}
void Ui::InventoryFilters() {
    for (uint32_t i = 0; i < 6; ++i)
        Button({32 + int(i) * 198, 120, 186, 72}, EquipmentFilters[i], HitKind::EquipmentFilter, 0, i);
}
void Ui::EquippedCharms() {
    Label({32, 120, 1176, 36}, L"EQUIPPED BONE CHARMS", 27, true, Muted);
    const auto slots = snapshot_->charmSlots;
    if (slots.known && slots.value >= 0 && slots.value <= 10) {
        const uint32_t count = std::min<uint32_t>(snapshot_->charms.domain.stored, snapshot_->charms.entries.size());
        for (int32_t slot = 0; slot < slots.value; ++slot) {
            const bridge::BoneCharm* charm = nullptr;
            for (uint32_t i = 0; i < count; ++i) {
                const auto& candidate = snapshot_->charms.entries[i];
                if (Yes(candidate.equipped) && candidate.equippedSlot.known && candidate.equippedSlot.value == slot) { charm = &candidate; break; }
            }
            const bool twoRows = slots.value > 5;
            const int columns = twoRows ? (slots.value + 1) / 2 : slots.value;
            const int width = (Content.w - (columns - 1) * 12) / columns;
            Box box{Content.x + (slot % columns) * (width + 12), 164 + (slot / columns) * 76, width, twoRows ? 68 : 144};
            canvas_.FillRect(box, charm && IsPressed(HitKind::Entry, charm->key) ? Color(36, 51, 54) : Surface);
            Border(box, charm != nullptr, charm ? charm->key : uint64_t(slot));
            if (charm) {
                if (twoRows) {
                    Image(charm->label.artwork.id, {box.x + 12, box.y + 10, 48, 48});
                    Label({box.x + 72, box.y + 12, box.w - 84, 44}, Text(charm->label.title), 20, true, Cream, true);
                } else {
                    const int icon = std::min(64, box.w - 48);
                    Image(charm->label.artwork.id, {box.x + (box.w - icon) / 2, box.y + 20, icon, 64});
                    Label({box.x + 20, box.bottom() - 48, box.w - 40, 32}, Text(charm->label.title), 22, false, Cream, false, dis::Align::kCenter);
                }
                hits_.push_back({box, HitKind::Entry, charm->key});
            } else {
                const auto occupancy = snapshot_->equippedCharms[uint32_t(slot)];
                Label(box.Inset(16, 12), occupancy.known && !occupancy.value ? L"EMPTY" : L"—", 24, false, Muted, false, dis::Align::kCenter);
            }
        }
        if (!slots.value) Label({60, 190, 1120, 80}, L"No bone charm slots available.", 30, false, Muted);
    } else Label({60, 190, 1120, 80}, L"Equipped slots are not yet available.", 30, false, Muted);
    Label({32, 324, 760, 34}, L"COLLECTED BONE CHARMS   " + std::to_wstring(snapshot_->charms.domain.stored), 27, true, Muted);
    Label({840, 324, 368, 34}, L"RUNES   " + Quantity(snapshot_->runes), 27, true, Cream, false, dis::Align::kRight);
}
void Ui::Documents() {
    const bool journal = page_ == bridge::Page::Journal;
    if (journal) for (uint32_t i = 0; i < 6; ++i)
        Button({32 + int(i) * 198, 120, 186, 72}, JournalFilters[i], HitKind::JournalFilter, 0, i);
    const int top = journal ? 208 : Content.y, height = Content.bottom() - top;
    auto& scroll = listScroll_[uint32_t(page_)];
    scroll.maximum = float(std::max(0, int(rows_.size()) * 164 - 12 - height));
    scroll.offset = std::clamp(scroll.offset, 0.0f, scroll.maximum);
    Box clip{32, top, 400, height}; listBox_ = clip; canvas_.SetClip(clip);
    int saved = SaveDC(canvas_.dc()); IntersectClipRect(canvas_.dc(), clip.x, clip.y, clip.right(), clip.bottom());
    for (uint32_t i = 0; i < rows_.size(); ++i) {
        const Row& row = rows_[i]; Box box{32, top + int(i) * 164 - int(scroll.offset), 384, 152};
        if (box.bottom() <= clip.y || box.y >= clip.bottom()) continue;
        bool selected = selected_[uint32_t(page_)] == row.key ||
            IsPressed(HitKind::Entry, row.key);
        canvas_.FillRect(box, selected ? Color(36, 48, 50) : Surface); Border(box, selected, row.key);
        const bool hasIcon = row.label->artwork.id && Image(row.label->artwork.id, {box.x + 24, box.y + 24, 56, 56});
        const int inset = (hasIcon ? 92 : 28) + (row.child ? 16 : 0);
        if (row.child) canvas_.Line(box.x + 12, box.y + 22, box.x + 12, box.bottom() - 22, 1, Brass);
        Label({box.x + inset, box.y + 20, box.w - inset - 24, 76}, Text(row.label->title), 24, true, Cream, true);
        Label({box.x + inset, box.bottom() - 40, box.w - inset - 24, 26}, row.annotation, 23, false, Muted);
        Box target{box.x, std::max(box.y, clip.y), box.w, std::min(box.bottom(), clip.bottom()) - std::max(box.y, clip.y)};
        if (target.h >= 64) hits_.push_back({target, HitKind::Entry, row.key, i, true});
    }
    RestoreDC(canvas_.dc(), saved); canvas_.ClearClip();
    ScrollIndicator(clip, scroll);
    auto found = std::find_if(rows_.begin(), rows_.end(), [this](const Row& row) { return row.key == selected_[uint32_t(page_)]; });
    if (!journal && found == rows_.end() && !rows_.empty() && rows_.front().key == ChapterKey) found = rows_.begin();
    int portraitsHeight = 0;
    if (!journal && found != rows_.end() && found->key == ChapterKey) {
        uint32_t column = 0;
        constexpr wchar_t const* states[] = {L"Eliminated", L"Neutralized", L"Rescued", L"Spared", L"Active"};
        for (const auto& target : snapshot_->chapterTargets) {
            if (!target.label.artwork.id) continue;
            Box box{456 + int(column++) * 384, top, 368, 180};
            canvas_.FillRect(box, Surface); Border(box);
            Image(target.label.artwork.id, {box.x + 24, box.y + 26, 112, 128});
            Label({box.x + 152, box.y + 24, 192, 76}, target.label.title.length ? Text(target.label.title) : L"Mission target", 28, true, Cream, true);
            if (target.state.known && uint32_t(target.state.value) < 5)
                Label({box.x + 152, box.y + 116, 192, 32}, states[uint32_t(target.state.value)], 26, false,
                    target.state.value == bridge::TargetState::Eliminated ? Red : Pale);
        }
        if (column) portraitsHeight = 198;
    }
    if (found != rows_.end()) Details(*found, {456, top + portraitsHeight, 752, height - portraitsHeight}, journal);
    else Label({484, top + 32, 696, 140}, L"Select an entry to read.", 34, false, Muted, true);
}
void Ui::ClampMap() {
    mapScale_ = std::clamp(mapScale_, 1.0f, 8.0f);
    const float width = mapWidth_ * mapScale_, height = mapHeight_ * mapScale_;
    mapX_ = width <= MapBox.w ? (MapBox.w - width) / 2 : std::clamp(mapX_, MapBox.w - width, 0.0f);
    mapY_ = height <= MapBox.h ? (MapBox.h - height) / 2 : std::clamp(mapY_, MapBox.h - height, 0.0f);
}
void Ui::Zoom(float factor, float anchorX, float anchorY) {
    const float old = mapScale_; mapScale_ = std::clamp(mapScale_ * factor, 1.0f, 8.0f);
    mapX_ = anchorX - (anchorX - mapX_) * mapScale_ / old;
    mapY_ = anchorY - (anchorY - mapY_) * mapScale_ / old;
    ClampMap(); dirty_ = true;
}
void Ui::Map() {
    mapView_ = snapshot_->map;
    if (preferredMap_) {
        const auto count = std::min<std::size_t>(mapView_.sheets.domain.stored, mapView_.sheets.entries.size());
        const auto end = mapView_.sheets.entries.begin() + count;
        const auto found = std::find_if(mapView_.sheets.entries.begin(), end,
            [this](const bridge::MapSheet& sheet) { return sheet.key == preferredMap_; });
        if (found == end) {
            if (mapView_.sheets.domain.availability == bridge::Availability::Available)
                preferredMap_ = 0; // Confirmed acquisition/level state changed.
            else {
                // A partial read cannot revoke the preference or substitute
                // another area's drawing. Do not retain old text-arena refs.
                mapView_.key = preferredMap_; mapView_.label = {}; mapView_.acquired = {};
                mapView_.legend = {}; mapView_.markers = {};
                mapView_.playerX = {}; mapView_.playerY = {}; mapView_.facingRadians = {}; mapView_.positionApproximate = false;
            }
        }
        else {
            if (mapView_.key != preferredMap_) {
                mapView_.playerX = found->playerX; mapView_.playerY = found->playerY; mapView_.facingRadians = found->facingRadians;
                mapView_.positionApproximate = found->positionApproximate;
                mapView_.markers = {}; // Never project another area's live markers onto this sheet.
            }
            mapView_.key = found->key; mapView_.label.title = found->title;
            mapView_.label.artwork = found->artwork; mapView_.legend = found->legend;
            mapView_.acquired = {true, true};
        }
    }
    mapView_.followingPlayer = preferredMap_ == 0;
    mapObjectiveCache_.Project(*snapshot_, mapView_, frameAt_);
    const auto& map = mapView_;
    if (map.key != mapKey_) { mapKey_ = map.key; mapScale_ = 1; mapX_ = mapY_ = 0; }
    canvas_.FillRect(MapBox, Surface);
    if (!Yes(map.acquired) || !map.label.artwork.id) {
        Border(MapBox);
        Label({160, 420, 920, 160}, map.followingPlayer ? L"No acquired map is available for this area." :
            L"The selected map is no longer available in this area.", 30, true, Muted, true);
        Button({56, 142, 250, 76}, mapSheetsOpen_ ? L"CLOSE MAPS" : L"ACQUIRED MAPS", HitKind::MapSheets,
            0, 0, map.sheets.domain.stored != 0);
        if (mapSheetsOpen_) MapSheets();
        return;
    }
    const auto image = resources_->images.find(map.label.artwork.id);
    if (image != resources_->images.end()) {
        const float width = float(image->second->GetWidth()), height = float(image->second->GetHeight());
        const float fit = std::min(MapBox.w / width, MapBox.h / height);
        mapWidth_ = width * fit; mapHeight_ = height * fit; ClampMap();
    }
    int saved = SaveDC(canvas_.dc()); IntersectClipRect(canvas_.dc(), MapBox.x, MapBox.y, MapBox.right(), MapBox.bottom());
    canvas_.SetClip(MapBox);
    const bool rendered = Image(map.label.artwork.id, {MapBox.x + int(mapX_), MapBox.y + int(mapY_), int(mapWidth_ * mapScale_), int(mapHeight_ * mapScale_)});
    if (rendered) {
        for (uint32_t i = 0; i < std::min<uint32_t>(map.markers.domain.stored, map.markers.entries.size()); ++i) {
            const auto& marker = map.markers.entries[i];
            if (!std::isfinite(marker.x) || !std::isfinite(marker.y) || marker.x < 0 || marker.x > 1 || marker.y < 0 || marker.y > 1) continue;
            int x = MapBox.x + int(mapX_ + marker.x * mapWidth_ * mapScale_);
            int y = MapBox.y + int(mapY_ + marker.y * mapHeight_ * mapScale_);
            if (marker.kind == bridge::MarkerKind::PrimaryObjective) {
                for (int pass = 0; pass < 2; ++pass) {
                    const auto color = pass ? Cream : Ground;
                    const int stroke = pass ? 2 : 6;
                    canvas_.Line(x, y-15, x+15, y, stroke, color);
                    canvas_.Line(x+15, y, x, y+15, stroke, color);
                    canvas_.Line(x, y+15, x-15, y, stroke, color);
                    canvas_.Line(x-15, y, x, y-15, stroke, color);
                }
            } else {
                canvas_.StrokeCircle(x, y, 13, 6, Ground);
                canvas_.StrokeCircle(x, y, 13, 2, Pale);
            }
            if (x >= MapBox.x + 32 && x < MapBox.right() - 32 && y >= MapBox.y + 32 && y < MapBox.bottom() - 32)
                hits_.push_back({{x - 32, y - 32, 64, 64}, HitKind::Marker, marker.key, i, true});
        }
        if (map.playerX.known && map.playerY.known &&
            std::isfinite(map.playerX.value) && std::isfinite(map.playerY.value) &&
            map.playerX.value >= 0 && map.playerX.value <= 1 && map.playerY.value >= 0 && map.playerY.value <= 1) {
            int x = MapBox.x + int(mapX_ + map.playerX.value * mapWidth_ * mapScale_);
            int y = MapBox.y + int(mapY_ + map.playerY.value * mapHeight_ * mapScale_);
            canvas_.FillCircle(x, y, 11, Ground);
            canvas_.FillCircle(x, y, 8, Red);
            canvas_.StrokeCircle(x, y, 17, 2, Ground);
            if (map.facingRadians.known && std::isfinite(map.facingRadians.value)) {
                const int tipX = x + int(std::cos(map.facingRadians.value) * 30);
                const int tipY = y + int(std::sin(map.facingRadians.value) * 30);
                canvas_.Line(x, y, tipX, tipY, 7, Ground);
                canvas_.Line(x, y, tipX, tipY, 3, Red);
            }
        }
    }
    RestoreDC(canvas_.dc(), saved); canvas_.ClearClip();
    Border(MapBox);
    Button({1112, 142, 76, 76}, L"+", HitKind::ZoomIn, 0, 0, rendered);
    Button({1112, 230, 76, 76}, L"−", HitKind::ZoomOut, 0, 0, rendered);
    Button({1002, 770, 186, 74}, L"RECENTER", HitKind::Recenter, 0, 0, rendered && map.playerX.known && map.playerY.known);
    Button({52, 142, 210, 76}, mapLegendOpen_ ? L"CLOSE LEGEND" : L"LEGEND", HitKind::MapLegend, 0, 0, rendered && map.legend.domain.stored);
    Button({268, 142, 250, 76}, mapSheetsOpen_ ? L"CLOSE MAPS" : L"ACQUIRED MAPS", HitKind::MapSheets,
        0, 0, map.sheets.domain.stored != 0);
    if (mapLegendOpen_) MapLegend();
    if (mapSheetsOpen_) MapSheets();
    Label({40, 884, 842, 46}, Text(map.label.title), 30, true);
    Label({898, 890, 302, 38}, map.positionApproximate ? L"Approximate position" :
        map.followingPlayer ? L"NEAREST ACQUIRED MAP" : L"SELECTED MAP", 23, true, Muted);
}
void Ui::MapSheets() {
    const Box panel{268, 234, 700, 610}, clip = panel.Inset(14);
    canvas_.FillRect(panel, Ground); Border(panel);
    hits_.push_back({panel, HitKind::MapLegendSurface}); listBox_ = panel;
    const auto& sheets = snapshot_->map.sheets;
    const uint32_t count = std::min<uint32_t>(sheets.domain.stored, sheets.entries.size());
    auto& scroll = listScroll_[uint32_t(bridge::Page::Map)];
    scroll.maximum = float(std::max(0, int(count + 1) * 92 - clip.h));
    scroll.offset = std::clamp(scroll.offset, 0.0f, scroll.maximum);
    int saved = SaveDC(canvas_.dc()); IntersectClipRect(canvas_.dc(), clip.x, clip.y, clip.right(), clip.bottom());
    canvas_.SetClip(clip);
    for (uint32_t i = 0; i <= count; ++i) {
        const Box row{clip.x, clip.y + int(i) * 92 - int(scroll.offset), clip.w, 84};
        if (row.bottom() <= clip.y || row.y >= clip.bottom()) continue;
        const auto key = i ? sheets.entries[i - 1].key : 0;
        const std::wstring title = i ? std::to_wstring(i) + L"   " + std::wstring(Text(sheets.entries[i - 1].title)) :
            L"FOLLOW NEAREST ACQUIRED MAP";
        Button(row, title, HitKind::MapSheet, key, 0, count != 0);
        // Only the visible part of a scrolled row may receive a touch.
        auto& hit = hits_.back().box;
        hit.y = std::max(row.y, clip.y); hit.h = std::min(row.bottom(), clip.bottom()) - hit.y;
        if (hit.h < 64) hits_.pop_back();
    }
    RestoreDC(canvas_.dc(), saved); canvas_.ClearClip();
    ScrollIndicator(clip, scroll);
}
void Ui::MapLegend() {
    const Box panel{52, 234, 450, 610}, clip = panel.Inset(14);
    canvas_.FillRect(panel, Ground); Border(panel);
    // This surface consumes taps so covered map markers cannot be activated.
    hits_.push_back({panel, HitKind::MapLegendSurface}); listBox_ = panel;
    const auto& legend = mapView_.legend;
    const uint32_t count = std::min<uint32_t>(legend.domain.stored, legend.entries.size());
    auto& scroll = listScroll_[uint32_t(bridge::Page::Map)];
    scroll.maximum = float(std::max(0, int(count) * 100 - clip.h));
    scroll.offset = std::clamp(scroll.offset, 0.0f, scroll.maximum);
    int saved = SaveDC(canvas_.dc()); IntersectClipRect(canvas_.dc(), clip.x, clip.y, clip.right(), clip.bottom());
    canvas_.SetClip(clip);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& entry = legend.entries[i];
        const Box row{clip.x, clip.y + int(i) * 100 - int(scroll.offset), clip.w, 94};
        if (row.bottom() <= clip.y || row.y >= clip.bottom()) continue;
        const bool numbered = entry.symbol >= 20 && entry.symbol <= 34;
        const int inset = numbered ? 66 : 8;
        if (numbered) {
            canvas_.StrokeCircle(row.x + 27, row.y + 38, 24, 1, Brass);
            Label({row.x + 9, row.y + 17, 40, 42}, std::to_wstring(entry.symbol - 19), 30, true);
        }
        Label({row.x + inset, row.y + 4, row.w - inset - 8, 82}, Text(entry.description), 28, false, Cream, true);
        canvas_.Line(row.x + 8, row.bottom(), row.right() - 8, row.bottom(), 1, Brass);
    }
    RestoreDC(canvas_.dc(), saved); canvas_.ClearClip();
    ScrollIndicator(clip, scroll);
}
void Ui::Statistics() {
    auto& scroll = listScroll_[uint32_t(page_)];
    Border({32, 120, 1176, 96});
    Image(4, {60, 144, 48, 48});
    Label({124, 142, 300, 52}, L"COINS   " + Quantity(snapshot_->coins), 30, true);
    Label({448, 142, 330, 52}, L"RUNES   " + Quantity(snapshot_->runes), 30, true);
    const auto chaos = snapshot_->chaos;
    Label({808, 142, 372, 52}, !chaos.known ? L"CHAOS   —" :
        chaos.value == bridge::ChaosLevel::High ? L"CHAOS   HIGH" : L"CHAOS   LOW", 30, true,
        chaos.known && chaos.value == bridge::ChaosLevel::High ? Red : Cream);
    Button({32, 232, 580, 64}, L"CURRENT MISSION", HitKind::StatisticsFilter, 0, 0);
    Button({628, 232, 580, 64}, L"SAVED MISSION RESULTS", HitKind::StatisticsFilter, 0, 1);
    std::array<uint32_t, bridge::StatisticCapacity> indices{}; uint32_t count = 0;
    for (uint32_t i = 0; i < std::min<uint32_t>(snapshot_->statistics.domain.stored, snapshot_->statistics.entries.size()); ++i)
        if ((snapshot_->statistics.entries[i].scope == bridge::StatisticScope::SavedMission) == savedStatistics_) indices[count++] = i;
    if (!count) { Label({60, 344, 1120, 140}, savedStatistics_ ? L"No saved mission results are available." : L"Statistics are not yet available.", 34, false, Muted, true); return; }
    scroll.maximum = float(std::max(0, int((count + 1) / 2) * 144 - 16 - 624));
    scroll.offset = std::clamp(scroll.offset, 0.0f, scroll.maximum);
    Box clip{32, 312, 1176, 624}; listBox_ = clip; canvas_.SetClip(clip);
    int saved = SaveDC(canvas_.dc()); IntersectClipRect(canvas_.dc(), clip.x, clip.y, clip.right(), clip.bottom());
    for (uint32_t i = 0; i < count; ++i) {
        const auto& row = snapshot_->statistics.entries[indices[i]];
        Box box{32 + int(i % 2) * 596, 312 + int(i / 2) * 144 - int(scroll.offset), 564, 128};
        if (box.bottom() <= clip.y || box.y >= clip.bottom()) continue;
        canvas_.FillRect(box, Surface); Border(box);
        Label({box.x + 24, box.y + 20, 296, 60}, Text(row.label.title), 24, true, Cream, true);
        Label({box.x + 24, box.y + 90, 352, 24}, row.context.length ? Text(row.context) : row.scope == bridge::StatisticScope::CurrentMission ? L"" :
            row.scope == bridge::StatisticScope::Campaign ? L"Campaign" : L"", 23, false, Muted);
        std::wstring value = L"—";
        if (row.value.known && std::isfinite(row.value.value)) {
            wchar_t number[64]{}; swprintf(number, 64, L"%.6g", double(row.value.value)); value = number;
        }
        if (row.format == bridge::StatisticFormat::Check && row.condition.known) value = row.condition.value ? L"Yes" : L"No";
        if (row.format == bridge::StatisticFormat::Chaos && row.value.known) value = row.value.value == 0 ? L"Low" : L"High";
        if (row.format == bridge::StatisticFormat::Number && row.maximum.known &&
            std::isfinite(row.maximum.value) && row.maximum.value > 0) {
            wchar_t maximum[64]{}; swprintf(maximum, 64, L" / %.6g", double(row.maximum.value));
            value += maximum;
        }
        Label({box.right() - 188, box.y + 24, 160, 48}, value, 32, true, Cream, false, dis::Align::kRight);
    }
    RestoreDC(canvas_.dc(), saved); canvas_.ClearClip();
    ScrollIndicator(clip, scroll);
}
bool Ui::Render(uint64_t now) {
    if (!canvas_.ready()) return false;
    if (!dirty_) return transitioning_ && ComposeTransition(now);
    frameAt_ = now;
    hits_.clear(); listBox_ = detailBox_ = {}; BuildRows(); Frame(now);
    if (!snapshot_ || snapshot_->lifecycle != bridge::Lifecycle::Live) {
        Label({118, 382, 1004, 150}, bootstrap_ == Bridge::Bootstrap::Failed ? L"The journal could not start." :
            bootstrap_ == Bridge::Bootstrap::Starting ? L"Opening the journal..." :
            L"The journal will return when Corvo is ready.", 38, true, Cream, true);
    } else {
        switch (page_) {
            case bridge::Page::Map: Map(); break;
            case bridge::Page::Powers: Cards(3, Content.y, Content.h - (selected_[uint32_t(page_)] ? 280 : 0)); break;
            case bridge::Page::Equipment:
                InventoryFilters(); Cards(3, 208, Content.bottom() - 208 - (selected_[uint32_t(page_)] ? 320 : 0)); break;
            case bridge::Page::BoneCharms:
                EquippedCharms(); Cards(3, 374, Content.bottom() - 374 - (selected_[uint32_t(page_)] ? 320 : 0)); break;
            case bridge::Page::Objectives:
            case bridge::Page::Journal: Documents(); break;
            case bridge::Page::Statistics: Statistics(); break;
            default: break;
        }
        if (page_ != bridge::Page::Map && page_ != bridge::Page::Statistics && rows_.empty()) {
            const Box empty = listBox_.h > 0 ? Box{listBox_.x + 24, listBox_.y + 24, listBox_.w - 48, std::min(140, listBox_.h - 48)} : Box{80, 350, 1080, 140};
            const bool knownEmpty = Domain().availability == bridge::Availability::Available;
            Label(empty, knownEmpty ? (page_ == bridge::Page::BoneCharms ? L"No bone charms collected." : L"No entries in this category.") :
                L"Waiting for this collection's game data.", 28, true, Muted, true);
        }
    }
    GdiFlush(); dirty_ = false;
    // Page switches happen after Update. Record the page actually drawn so
    // the next snapshot does not redraw it merely to catch up this stamp.
    if (snapshot_) visibleState_ = VisibleState(*snapshot_, page_, fresh_, now);
    if (transitioning_) ComposeTransition(now);
    return true;
}
int Ui::HitAt(float x, float y) const noexcept {
    for (std::size_t i = hits_.size(); i > 0; --i) if (hits_[i - 1].box.Contains(int(x), int(y))) return int(i - 1);
    return -1;
}
bool Ui::IsPressed(HitKind kind, bridge::Key key, uint32_t value) const noexcept {
    return pressed_ && !moved_ && !pinching_ && hasPressedTarget_ && pressedTarget_.enabled &&
        pressedTarget_.kind == kind && pressedTarget_.key == key &&
        (kind == HitKind::Entry || kind == HitKind::Marker || pressedTarget_.value == value);
}
void Ui::Cancel() noexcept { pressed_ = moved_ = pinching_ = hasPressedTarget_ = false; dragRegion_ = gestureAxis_ = 0; transitioning_ = false; dirty_ = true; }
void Ui::ChangePage(bridge::Page page, uint64_t now) {
    if (page == page_ || uint32_t(page) >= 7) return;
    const bool captured = previousPage_.ready() && BitBlt(previousPage_.dc(), 0, 0, 1240, 1080, Canvas().dc(), 0, 0, SRCCOPY);
    const int direction = (uint32_t(page) + 7 - uint32_t(page_)) % 7 == 1 ? 1 : -1;
    Cancel(); page_ = page; hits_.clear(); dirty_ = true;
    transitionDirection_ = direction; transitionStarted_ = now ? now : frameAt_;
    transitioning_ = captured && composed_.ready();
}
void Ui::ScrollIndicator(Box box, const Scroll& scroll) {
    if (scroll.maximum <= 0 || box.h < 64) return;
    const int height = box.h - 16;
    const int thumb = std::max(36, int(height * box.h / (box.h + scroll.maximum)));
    const int y = box.y + 8 + int((height - thumb) * std::clamp(scroll.offset / scroll.maximum, 0.0f, 1.0f));
    canvas_.FillRect({box.right() - 5, box.y + 8, 2, height}, Brass.Alpha(70));
    canvas_.FillRect({box.right() - 6, y, 4, thumb}, Pale.Alpha(170));
}
bool Ui::ComposeTransition(uint64_t now) {
    if (!transitioning_) return false;
    const float elapsed = float(now >= transitionStarted_ ? now - transitionStarted_ : 240);
    if (elapsed >= 240) { transitioning_ = false; return true; }
    const float t = elapsed / 240.0f, eased = 1 - std::pow(1 - t, 3.0f);
    const int shift = int(Content.w * eased) * transitionDirection_;
    HDC target = composed_.dc();
    BitBlt(target, 0, 0, 1240, 1080, canvas_.dc(), 0, 0, SRCCOPY);
    const int saved = SaveDC(target);
    IntersectClipRect(target, Content.x, Content.y, Content.right(), Content.bottom());
    BitBlt(target, Content.x - shift, Content.y, Content.w, Content.h, previousPage_.dc(), Content.x, Content.y, SRCCOPY);
    BitBlt(target, Content.x + transitionDirection_ * Content.w - shift, Content.y, Content.w, Content.h,
        canvas_.dc(), Content.x, Content.y, SRCCOPY);
    RestoreDC(target, saved); GdiFlush();
    return true;
}
void Ui::Submit(bridge::CommandKind kind, bridge::Key key, uint64_t now) {
    if (!snapshot_ || pendingRequest_ || !bridge::Fresh(*snapshot_, now)) return;
    bridge::Command command{}; command.request = ++nextRequest_; command.generation = generation_;
    command.domainRevision = Domain().revision; command.issuedAt = now;
    command.kind = kind; command.key = key;
    if (bridge_.commands.Push(command)) {
        pendingRequest_ = command.request; pendingKind_ = kind; pendingSince_ = now;
        feedback_ = kind == bridge::CommandKind::ReadDocument ? L"Opening document…" :
            kind == bridge::CommandKind::PlayAudiograph ? L"Starting recording…" :
            kind == bridge::CommandKind::StopAudiograph ? L"Stopping recording…" : L"Selecting…";
    } else { feedback_ = L"Please try again."; feedbackUntil_ = now + 1800; }
    dirty_ = true;
}
void Ui::Activate(const Hit& hit, uint64_t now, bool detailsOnly) {
    if (!hit.enabled) return;
    if (hit.kind == HitKind::CloseDetails) {
        Cancel(); selected_[uint32_t(page_)] = 0; detailScroll_[uint32_t(page_)] = {}; return;
    }
    if (hit.kind == HitKind::JournalFilter && hit.value < 6) {
        Cancel(); journalFilter_ = hit.value; listScroll_[uint32_t(page_)] = {}; dirty_ = true; return;
    }
    if (hit.kind == HitKind::EquipmentFilter && hit.value < 6) {
        Cancel(); equipmentFilter_ = hit.value; listScroll_[uint32_t(page_)] = {}; dirty_ = true; return;
    }
    if (hit.kind == HitKind::StatisticsFilter && hit.value < 2) {
        Cancel(); savedStatistics_ = hit.value != 0; listScroll_[uint32_t(page_)] = {}; dirty_ = true; return;
    }
    if (hit.kind == HitKind::MapLegend) {
        Cancel(); mapLegendOpen_ = !mapLegendOpen_; mapSheetsOpen_ = false;
        listScroll_[uint32_t(bridge::Page::Map)] = {}; return;
    }
    if (hit.kind == HitKind::MapSheets) {
        Cancel(); mapSheetsOpen_ = !mapSheetsOpen_; mapLegendOpen_ = false;
        listScroll_[uint32_t(bridge::Page::Map)] = {}; return;
    }
    if (!snapshot_ || snapshot_->lifecycle != bridge::Lifecycle::Live) return;
    if (hit.kind == HitKind::MapSheet) {
        const auto& sheets = snapshot_->map.sheets;
        const auto end = sheets.entries.begin() + std::min<std::size_t>(sheets.domain.stored, sheets.entries.size());
        if (hit.key && std::find_if(sheets.entries.begin(), end,
            [&hit](const bridge::MapSheet& sheet) { return sheet.key == hit.key; }) == end) return;
        preferredMap_ = hit.key;
        mapSheetsOpen_ = false; listScroll_[uint32_t(bridge::Page::Map)] = {}; dirty_ = true; return;
    }
    if (hit.kind == HitKind::Entry) {
        if (page_ == bridge::Page::Equipment && !detailsOnly && bridge::Fresh(*snapshot_, now)) {
            for (uint32_t i = 0; i < std::min<uint32_t>(snapshot_->equipment.domain.stored, snapshot_->equipment.entries.size()); ++i) {
                const auto& item = snapshot_->equipment.entries[i];
                if (item.key != hit.key) continue;
                if (item.kind == bridge::EquipmentKind::Weapon || item.kind == bridge::EquipmentKind::Gadget ||
                    item.kind == bridge::EquipmentKind::Ammunition) {
                    selected_[uint32_t(page_)] = 0; dirty_ = true;
                    if (!Yes(item.selected) && Yes(item.selectable) && !pendingRequest_)
                        Submit(bridge::CommandKind::SelectEquipment, hit.key, now);
                    return;
                }
                break;
            }
        }
        selected_[uint32_t(page_)] = hit.key; detailScroll_[uint32_t(page_)] = {}; dirty_ = true;
        if (page_ == bridge::Page::Journal) for (uint32_t i = 0; i < std::min<uint32_t>(snapshot_->journal.domain.stored, snapshot_->journal.entries.size()); ++i)
            if (snapshot_->journal.entries[i].key == hit.key && !snapshot_->journal.entries[i].contentsComplete) {
                Submit(bridge::CommandKind::ReadDocument, hit.key, now); break;
            }
        return;
    }
    if (hit.kind == HitKind::Action && !pendingRequest_) {
        Submit(bridge::CommandKind(hit.value), hit.key, now); return;
    }
    if (hit.kind == HitKind::ZoomIn) Zoom(1.25f, MapBox.w / 2.0f, MapBox.h / 2.0f);
    if (hit.kind == HitKind::ZoomOut) Zoom(0.8f, MapBox.w / 2.0f, MapBox.h / 2.0f);
    if (hit.kind == HitKind::Recenter) {
        mapX_ = MapBox.w / 2.0f - mapView_.playerX.value * mapWidth_ * mapScale_;
        mapY_ = MapBox.h / 2.0f - mapView_.playerY.value * mapHeight_ * mapScale_;
        ClampMap(); dirty_ = true;
    }
    if (hit.kind == HitKind::Marker && hit.value < mapView_.markers.domain.stored && hit.value < mapView_.markers.entries.size()) {
        feedback_ = Text(mapView_.markers.entries[hit.value].label.title); feedbackUntil_ = now + 3500; dirty_ = true;
    }
}
bool Ui::Input(const Touch& event, uint64_t now) {
    if (event.gesture != Gesture::Cancel && event.gesture != Gesture::NextPage && event.gesture != Gesture::PreviousPage &&
        (!std::isfinite(event.x) || !std::isfinite(event.y) || event.x < 0 || event.x > 1 || event.y < 0 || event.y > 1)) return false;
    // A batch may change page/selection before its next click. Resolve click
    // targets from the current layout; defer redraws for intermediate moves.
    // Release uses the semantic target captured on down. A changed snapshot
    // has already rebuilt layout in the panel pump, and a moved drag cannot
    // activate a row. Do not rasterize an entire pressed frame before handling
    // the release when both packets are waiting in the same input batch.
    if (dirty_ && (event.gesture == Gesture::Down || event.gesture == Gesture::Tap || event.gesture == Gesture::Hold)) Render(now);
    const float x = event.x * 1240, y = event.y * 1080;
    if (event.gesture == Gesture::Cancel) { Cancel(); return true; }
    if (event.gesture == Gesture::NextPage || event.gesture == Gesture::PreviousPage) {
        ChangePage(bridge::Page((uint32_t(page_) + (event.gesture == Gesture::NextPage ? 1 : 6)) % 7), now); return true;
    }
    if (transitioning_) return true; // No action on a control while its image is moving.
    if (event.gesture == Gesture::Pinch) {
        if (!std::isfinite(event.factor) || event.factor <= 0) return false;
        if (page_ == bridge::Page::Map && MapBox.Contains(int(x), int(y)) && !listBox_.Contains(int(x), int(y))) {
            Cancel(); pinching_ = true; Zoom(event.factor, x - MapBox.x, y - MapBox.y); return true;
        }
        return false;
    }
    if (event.gesture == Gesture::Down) {
        Cancel(); pressed_ = true; downX_ = lastX_ = x; downY_ = lastY_ = y;
        const int target = HitAt(x, y);
        hasPressedTarget_ = target >= 0;
        if (hasPressedTarget_) pressedTarget_ = hits_[target];
        lastInput_ = now;
        dragRegion_ = page_ == bridge::Page::Map && MapBox.Contains(int(x), int(y)) ? 3 : 0;
        if (listBox_.Contains(int(x), int(y))) dragRegion_ = 1;
        if (detailBox_.Contains(int(x), int(y))) dragRegion_ = 2;
        listScroll_[uint32_t(page_)].velocity = detailScroll_[uint32_t(page_)].velocity = 0;
        return true;
    }
    if (event.gesture == Gesture::Move && pressed_) {
        if (std::hypot(x - downX_, y - downY_) > 12) moved_ = true;
        if (!gestureAxis_ && dragRegion_ != 3) {
            const float dx = std::abs(x - downX_), dy = std::abs(y - downY_);
            if (dx >= 18 && dx > dy * 1.35f) gestureAxis_ = 1;
            else if (dy >= 18 && dy > dx * 1.15f) gestureAxis_ = 2;
        }
        if (moved_ && dragRegion_ && (dragRegion_ == 3 || gestureAxis_ == 2)) {
            if (dragRegion_ == 3) { mapX_ += x - lastX_; mapY_ += y - lastY_; ClampMap(); }
            else {
                auto& scroll = dragRegion_ == 2 ? detailScroll_[uint32_t(page_)] : listScroll_[uint32_t(page_)];
                float delta = lastY_ - y;
                scroll.offset = std::clamp(scroll.offset + delta, 0.0f, scroll.maximum);
                float dt = float(std::max<uint64_t>(1, now >= lastInput_ ? now - lastInput_ : 1)) / 1000;
                scroll.velocity = std::clamp(delta / dt, -2400.0f, 2400.0f);
            }
        }
        lastX_ = x; lastY_ = y; lastInput_ = now; dirty_ = true; return true;
    }
    if (event.gesture == Gesture::Up) {
        const float dx = x - downX_, dy = y - downY_;
        const bool swipe = pressed_ && !pinching_ && dragRegion_ != 3 && gestureAxis_ != 2 &&
            std::abs(dx) >= 96 && std::abs(dx) > std::abs(dy) * 1.35f;
        if (swipe) {
            ChangePage(bridge::Page((uint32_t(page_) + (dx < 0 ? 1 : 6)) % 7), now);
            lastTap_ = now; return true;
        }
        int hit = HitAt(x, y);
        const bool activate = hit >= 0 && hits_[hit].enabled &&
            IsPressed(hits_[hit].kind, hits_[hit].key, hits_[hit].value);
        pressed_ = hasPressedTarget_ = pinching_ = false;
        if (activate) { Hit selected = hits_[hit]; Activate(selected, now); }
        // Suppress a trailing Android tap after a drag or pinch as well as a
        // click; releasing a map gesture must never activate a nearby marker.
        lastTap_ = now;
        dirty_ = true; return true;
    }
    if (event.gesture == Gesture::Tap && (!lastTap_ || now - lastTap_ > 100)) {
        int hit = HitAt(x, y); if (hit >= 0) { Hit selected = hits_[hit]; Activate(selected, now); lastTap_ = now; }
        return true;
    }
    if (event.gesture == Gesture::Hold) {
        const int target = HitAt(x, y);
        // Holding a power/item opens its details, as in the supplied mockup.
        // It must never execute the neighboring equip/use action on a hold.
        // Stock WEMU sends a standalone hold on release; the launcher's
        // continuous protocol already has a matching pressed entry.
        if (target >= 0 && hits_[target].kind == HitKind::Entry &&
            (!pressed_ || IsPressed(HitKind::Entry, hits_[target].key))) {
            const Hit selected = hits_[target]; Activate(selected, now, true);
        }
        Cancel(); lastTap_ = now; return true;
    }
    return false;
}
bool Ui::Animate(uint64_t now) {
    // UDP can lose a release packet when the lower surface disappears. A
    // stranded press must not survive indefinitely or activate a later tap.
    if (pressed_ && (now < lastInput_ || now - lastInput_ > 2000)) Cancel();
    const float dt = std::min(0.05f, float(now >= lastAnimation_ ? now - lastAnimation_ : 0) / 1000);
    lastAnimation_ = now;
    if (!pressed_) for (Scroll* scroll : {&listScroll_[uint32_t(page_)], &detailScroll_[uint32_t(page_)]}) {
        if (std::abs(scroll->velocity) < 4) { scroll->velocity = 0; continue; }
        scroll->offset = std::clamp(scroll->offset + scroll->velocity * dt, 0.0f, scroll->maximum);
        scroll->velocity *= std::exp(-7.5f * dt);
        if (scroll->offset == 0 || scroll->offset == scroll->maximum) scroll->velocity = 0;
        dirty_ = true;
    }
    if (pendingRequest_ && (!fresh_ || now < pendingSince_ || now - pendingSince_ > 2000)) {
        const bool audio = pendingKind_ == bridge::CommandKind::PlayAudiograph || pendingKind_ == bridge::CommandKind::StopAudiograph;
        pendingRequest_ = 0; feedback_ = audio ? L"Playback was not confirmed." :
            pendingKind_ == bridge::CommandKind::ReadDocument ? L"Document was not available." : L"Selection was not confirmed.";
        feedbackUntil_ = now + 2200; dirty_ = true;
    }
    return dirty_ || transitioning_;
}

} // namespace dishonored
