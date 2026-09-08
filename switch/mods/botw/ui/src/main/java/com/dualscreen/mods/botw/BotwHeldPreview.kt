package com.dualscreen.mods.botw

import org.json.JSONArray
import org.json.JSONObject
import kotlin.math.abs
import kotlin.math.acos
import kotlin.math.sin
import kotlin.math.sqrt

/** Column-major affine transforms, in the game's model units (not screen coordinates). */
internal object BotwHeldTransforms {
    fun multiply(a: FloatArray, b: FloatArray): FloatArray = FloatArray(16) { i ->
        val row = i % 4
        val column = i / 4
        (0..3).sumOf { k -> a[k * 4 + row].toDouble() * b[column * 4 + k] }.toFloat()
    }

    fun inverse(m: FloatArray): FloatArray {
        require(m.size == 16 && m.all(Float::isFinite))
        val a = m[0]; val b = m[4]; val c = m[8]
        val d = m[1]; val e = m[5]; val f = m[9]
        val g = m[2]; val h = m[6]; val i = m[10]
        val det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
        require(abs(det) > 1e-8f && det.isFinite())
        val r = floatArrayOf(
            (e*i-f*h)/det, (f*g-d*i)/det, (d*h-e*g)/det, 0f,
            (c*h-b*i)/det, (a*i-c*g)/det, (b*g-a*h)/det, 0f,
            (b*f-c*e)/det, (c*d-a*f)/det, (a*e-b*d)/det, 0f,
            0f, 0f, 0f, 1f
        )
        for (row in 0..2) r[12+row] = -(r[row]*m[12] + r[4+row]*m[13] + r[8+row]*m[14])
        return r
    }

    fun relative(parent: FloatArray, child: FloatArray) = multiply(inverse(parent), child)

    /** Blend skeletal TRS, never matrix coefficients (which shrink rotating hands). */
    fun interpolate(a: FloatArray, b: FloatArray, weight: Float): FloatArray {
        val t = weight.coerceIn(0f, 1f)
        if (t == 0f) return a.copyOf()
        if (t == 1f) return b.copyOf()
        fun scales(m: FloatArray) = FloatArray(3) { c ->
            sqrt((0..2).sumOf { r -> (m[c*4+r] * m[c*4+r]).toDouble() }).toFloat()
                .also { require(it > 1e-8f && it.isFinite()) }
        }
        fun quaternion(m: FloatArray, s: FloatArray): FloatArray {
            val r = FloatArray(9) { i -> m[(i/3)*4+i%3] / s[i/3] }
            val trace = r[0]+r[4]+r[8]
            val q = FloatArray(4)
            if (trace > 0f) {
                val k = sqrt(trace+1f)*2f
                q[3]=k/4f; q[0]=(r[5]-r[7])/k; q[1]=(r[6]-r[2])/k; q[2]=(r[1]-r[3])/k
            } else {
                val i = (0..2).maxBy { r[it*3+it] }
                val j = (i+1)%3; val k = (i+2)%3
                val n = sqrt(1f+r[i*3+i]-r[j*3+j]-r[k*3+k])*2f
                q[i]=n/4f; q[j]=(r[j*3+i]+r[i*3+j])/n
                q[k]=(r[k*3+i]+r[i*3+k])/n; q[3]=(r[j*3+k]-r[k*3+j])/n
            }
            return q
        }
        val sa=scales(a); val sb=scales(b)
        val qa=quaternion(a,sa); val qb=quaternion(b,sb)
        var dot=(0..3).sumOf { (qa[it]*qb[it]).toDouble() }.toFloat()
        if (dot < 0f) { for (i in 0..3) qb[i] = -qb[i]; dot = -dot }
        val angle=acos(dot.coerceIn(0f,1f))
        val wa=if (dot > .9995f) 1f-t else sin((1f-t)*angle)/sin(angle)
        val wb=if (dot > .9995f) t else sin(t*angle)/sin(angle)
        val q=FloatArray(4) { qa[it]*wa+qb[it]*wb }
        val length=sqrt(q.sumOf { (it*it).toDouble() }).toFloat()
        for (i in 0..3) q[i]/=length
        val (x,y,z,w)=q
        val result=floatArrayOf(
            1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w),0f,
            2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w),0f,
            2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y),0f,
            a[12]+(b[12]-a[12])*t,a[13]+(b[13]-a[13])*t,a[14]+(b[14]-a[14])*t,1f)
        for (c in 0..2) for (r in 0..2) result[c*4+r] *= sa[c]+(sb[c]-sa[c])*t
        return result
    }
}

internal data class BotwHeldModel(val id: Long, val actor: String, val handLocal: FloatArray)
internal data class BotwHeldPreview(
    val playerId: Long,
    val bones: Map<String, FloatArray>,
    val items: List<BotwHeldModel>
) {
    /** Keep the portrait's attachments stationary, not the gameplay carrying sway.
     * Capture real hand-local placement again whenever any slot/actor/player changes.
     * Native CarryBox scale and pivot can still settle together after spawning.
     * Never combine the old spawn pivot with a newer scale (large meat exposes it).
     */
    fun stationaryAttachments(previous: BotwHeldPreview?): BotwHeldPreview {
        if (previous == null || playerId != previous.playerId || items.size != previous.items.size ||
            items.indices.any { items[it].id != previous.items[it].id ||
                items[it].actor != previous.items[it].actor }) return this
        return copy(items = items.mapIndexed { index, live ->
            val anchored = previous.items[index]
            var changed = false
            for (column in 0..2) {
                fun scale(m: FloatArray) = sqrt((0..2).sumOf { row ->
                    val value = m[column * 4 + row].toDouble()
                    value * value
                }).toFloat()
                val before = scale(anchored.handLocal)
                val after = scale(live.handLocal)
                // Ignore float noise. A real size transition invalidates the
                // entire captured placement, not just its three basis vectors.
                if (before > 1e-8f && abs(after - before) > 1e-4f * before) {
                    changed = true
                }
            }
            if (changed) live else anchored
        })
    }

    companion object {
        const val HAND = "Armature_Weapon_R"

        fun parse(json: String): BotwHeldPreview? {
            val state = JSONObject(json)
            if (state.optString("status") != "ok") return null
            val items = state.getJSONArray("items")
            require(items.length() in 0..5)
            if (items.length() == 0) return BotwHeldPreview(0, emptyMap(), emptyList())
            fun matrix(array: JSONArray): FloatArray {
                require(array.length() == 16)
                return FloatArray(16) { array.getDouble(it).toFloat() }.also {
                    require(it.all(Float::isFinite) && it[15] == 1f)
                }
            }
            val root = matrix(state.getJSONArray("root"))
            val bones = state.getJSONArray("bones")
            require(bones.length() in 1..256)
            val worlds = mutableListOf<FloatArray>()
            val locals = linkedMapOf<String, FloatArray>()
            var hand: FloatArray? = null
            for (index in 0 until bones.length()) {
                val bone = bones.getJSONArray(index)
                val name = bone.getString(0)
                val parent = bone.getInt(1)
                require(parent in -1 until index && name !in locals)
                val world = matrix(bone.getJSONArray(2))
                locals[name] = BotwHeldTransforms.relative(if (parent < 0) root else worlds[parent], world)
                worlds.add(world)
                if (name == HAND) hand = world
            }
            val handWorld = requireNotNull(hand)
            val held = (0 until items.length()).map { index ->
                val item = items.getJSONObject(index)
                val actor = item.getString("actor")
                require(actor.matches(Regex("(?:Item_|Animal_Insect_)[A-Za-z0-9_]+")))
                BotwHeldModel(item.getLong("id"), actor,
                    BotwHeldTransforms.relative(handWorld, matrix(item.getJSONArray("world"))))
            }
            require(held.map { it.id }.distinct().size == held.size)
            return BotwHeldPreview(state.getLong("player"), locals, held)
        }
    }
}
