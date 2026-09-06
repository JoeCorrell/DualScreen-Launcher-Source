package org.yuzu.yuzu_emu.activities

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.opengl.Matrix
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.Choreographer
import com.google.android.filament.*
import com.google.android.filament.gltfio.*
import org.json.JSONObject
import org.yuzu.yuzu_emu.utils.Log
import java.nio.ByteBuffer
import java.util.concurrent.Executors

/** Odyssey-only Filament portrait; no guest writes, UI-menu objects or gameplay ticks.
 * Uses the proven offscreen/Canvas presentation path without changing BOTW's renderer.
 */
internal class SmoPlayerRenderView(context: Context) : android.view.View(context), Choreographer.FrameCallback {
    private data class Part(val model: String, val joint: String?, val transform: FloatArray,
                            val child: Boolean = false, val hidden: Set<String> = emptySet())
    private data class Source(val part: Part, val bytes: ByteArray)
    private val handler = Handler(Looper.getMainLooper())
    private var io = Executors.newSingleThreadExecutor { task -> Thread(task, "SMO-preview-assets").apply { isDaemon = true } }
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
    private var token = 0
    private var appearance = ""
    private var active = false
    private var attached = false
    private var loading = false
    private var retryAt = 0L
    private var failed = false
    private var e: Engine? = null
    private var renderer: Renderer? = null
    private var scene: Scene? = null
    private var view: com.google.android.filament.View? = null
    private var camera: Camera? = null
    private var cameraEntity = 0
    private var lightEntity = 0
    private var ambient: IndirectLight? = null
    private var sky: Skybox? = null
    private var provider: UbershaderProvider? = null
    private var loader: AssetLoader? = null
    private var resources: ResourceLoader? = null
    private var color: Texture? = null
    private var depth: Texture? = null
    private var target: RenderTarget? = null
    private var chain: SwapChain? = null
    private val models = ArrayList<FilamentAsset>()
    private var pixels: ByteBuffer? = null
    private var portrait: Bitmap? = null
    private var pending = false
    private var pendingAt = 0L
    private var framePosted = false
    private var firstFrameLogged = false
    // Parsed once on the asset worker; never load the whole wardrobe into GPU memory.
    private var previewIndex: JSONObject? = null
    // Match the enlarged card's aspect ratio; stretching the old readback would
    // distort Mario instead of enlarging him. Still render only on appearance changes.
    private val renderWidth = SmoUiLayout.PLAYER_RENDER_WIDTH
    private val renderHeight = SmoUiLayout.PLAYER_RENDER_HEIGHT

    fun setAppearance(outfit: String, hat: String, visible: Boolean) {
        active = visible
        alpha = if (visible) 1f else 0f
        val next = if (visible) "$outfit/$hat" else ""
        if (next != appearance) {
            appearance = next; ++token; loading = false; retryAt = 0
            portrait = null; invalidate()
            e?.let { engine -> clearModels(engine) }
        }
        if (!active || !attached || failed) return
        if (models.isEmpty() && !loading && SystemClock.uptimeMillis() >= retryAt) load(outfit, hat)
        schedule()
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow(); attached = true
        if (io.isShutdown) io = Executors.newSingleThreadExecutor()
    }

    private fun load(outfit: String, hat: String) {
        val generation = token; loading = true
        io.execute {
            val result = runCatching {
                val index = previewIndex ?: context.assets.open("smo/player/index.json").bufferedReader()
                    .use { JSONObject(it.readText()) }.also { previewIndex = it }
                require(index.getInt("version") == 2) { "Incomplete legacy preview pack" }
                val body = index.getJSONObject("appearances").getJSONObject(outfit).getJSONObject(hat)
                val specs = ArrayList<Part>()
                specs.add(Part(body.getString("model"), null, FloatArray(16).apply {
                    Matrix.setIdentityM(this, 0); Matrix.rotateM(this, 0, -18f, 0f, 1f, 0f)
                    Matrix.scaleM(this, 0, .01f, .01f, .01f)
                }))
                val parts = body.getJSONArray("parts")
                for (i in 0 until parts.length()) {
                    val part = parts.getJSONObject(i)
                    val rotation = part.getJSONArray("rotation"); val translation = part.getJSONArray("translation")
                    val transform = FloatArray(16).apply {
                        Matrix.setIdentityM(this, 0)
                        Matrix.translateM(this, 0, translation.getDouble(0).toFloat(), translation.getDouble(1).toFloat(), translation.getDouble(2).toFloat())
                        Matrix.rotateM(this, 0, rotation.getDouble(2).toFloat(), 0f, 0f, 1f)
                        Matrix.rotateM(this, 0, rotation.getDouble(1).toFloat(), 0f, 1f, 0f)
                        Matrix.rotateM(this, 0, rotation.getDouble(0).toFloat(), 1f, 0f, 0f)
                        val scale = part.getJSONArray("scale")
                        Matrix.scaleM(this, 0, scale.getDouble(0).toFloat(), scale.getDouble(1).toFloat(), scale.getDouble(2).toFloat())
                    }
                    val hidden = part.getJSONArray("hidden")
                    specs.add(Part(part.getString("model"),
                        if (part.isNull("joint")) null else part.getString("joint"), transform, true,
                        (0 until hidden.length()).map { hidden.getString(it) }.toSet()))
                }
                require(specs.size in 2..12)
                specs.map { spec ->
                    require(spec.model.matches(Regex("[A-Za-z0-9_]{1,80}")))
                    val bytes = context.assets.open("smo/player/${spec.model}.glb").use { it.readBytes() }
                    require(bytes.size in 28..16 * 1024 * 1024)
                    Source(spec, bytes)
                }
            }
            handler.post {
                if (!attached || generation != token) return@post
                loading = false
                result.fold(onSuccess = { sources ->
                    runCatching { initialize(); install(sources) }.onFailure { fail("model setup", it) }
                }, onFailure = {
                    retryAt = SystemClock.uptimeMillis() + 15000
                    Log.warning("[SMO] Preview assets unavailable for $outfit/$hat: ${it.message}")
                })
            }
        }
    }

    private fun initialize() {
        if (e != null) return
        Filament.init(); Gltfio.init()
        val engine = Engine.create(Engine.Backend.OPENGL); e = engine
        val entities = EntityManager.get()
        renderer = engine.createRenderer().apply {
            clearOptions = Renderer.ClearOptions().apply {
                clear = true; discard = true; clearColor = doubleArrayOf(0.0,0.0,0.0,0.0)
            }
        }
        scene = engine.createScene()
        cameraEntity = entities.create()
        camera = engine.createCamera(cameraEntity).apply {
            setExposure(8f, 1f / 125f, 100f)
            setProjection(38.0, renderWidth.toDouble()/renderHeight, .05, 20.0, Camera.Fov.VERTICAL)
            // Closer portrait framing, leaving the full hat and shoes inside the postcard.
            lookAt(0.0, .75, 2.5, 0.0, .75, 0.0, 0.0, 1.0, 0.0)
        }
        view = engine.createView().apply {
            scene = this@SmoPlayerRenderView.scene; camera = this@SmoPlayerRenderView.camera
            blendMode = com.google.android.filament.View.BlendMode.TRANSLUCENT
            antiAliasing = com.google.android.filament.View.AntiAliasing.FXAA
            isPostProcessingEnabled = true
            setShadowingEnabled(false)
            viewport = Viewport(0,0,renderWidth,renderHeight)
        }
        lightEntity = entities.create()
        LightManager.Builder(LightManager.Type.DIRECTIONAL).color(1f,.96f,.88f).intensity(65000f)
            .direction(-.4f,-.6f,-.8f).castShadows(false).build(engine,lightEntity)
        scene!!.addEntity(lightEntity)
        ambient = IndirectLight.Builder().irradiance(1,floatArrayOf(.8f,.78f,.74f)).intensity(14000f).build(engine)
        scene!!.indirectLight = ambient
        sky = Skybox.Builder().color(0f,0f,0f,0f).build(engine); scene!!.skybox = sky
        provider = UbershaderProvider(engine)
        loader = AssetLoader(engine,provider!!,entities); resources = ResourceLoader(engine)
        color = Texture.Builder().width(renderWidth).height(renderHeight).levels(1).sampler(Texture.Sampler.SAMPLER_2D)
            .format(Texture.InternalFormat.RGBA8).usage(Texture.Usage.COLOR_ATTACHMENT or Texture.Usage.BLIT_SRC).build(engine)
        depth = Texture.Builder().width(renderWidth).height(renderHeight).levels(1).sampler(Texture.Sampler.SAMPLER_2D)
            .format(Texture.InternalFormat.DEPTH16).usage(Texture.Usage.DEPTH_ATTACHMENT).build(engine)
        target = RenderTarget.Builder().texture(RenderTarget.AttachmentPoint.COLOR,color!!)
            .texture(RenderTarget.AttachmentPoint.DEPTH,depth!!).build(engine)
        view!!.renderTarget = target
        chain = engine.createSwapChain(renderWidth,renderHeight,SwapChainFlags.CONFIG_DEFAULT)
        pixels = ByteBuffer.allocateDirect(renderWidth*renderHeight*4)
    }

    private fun install(sources: List<Source>) {
        val engine = e ?: return
        clearModels(engine)
        val tm = engine.transformManager
        try {
            sources.forEach { source ->
                val buffer = ByteBuffer.allocateDirect(source.bytes.size).apply { put(source.bytes); flip() }
                val asset = loader!!.createAsset(buffer) ?: error("Invalid GLB ${source.part.model}")
                models.add(asset)
                resources!!.loadResources(asset)
                val root = tm.getInstance(asset.root)
                if (source.part.child) {
                    val body = models.first()
                    val joint = source.part.joint
                    val entity = if (joint == null) body.root else
                        body.entities.firstOrNull { body.getName(it) == joint }
                            ?: error("Missing native attachment $joint")
                    tm.setParent(root, tm.getInstance(entity))
                }
                tm.setTransform(root,source.part.transform)
                // Native accessories with no AnimArc keep their authored local pose.
                // They still inherit the body's bone transform and skin matrices.
                asset.entities.forEach { entity ->
                    val name = asset.getName(entity)?.substringBefore("__")
                    if (name in source.part.hidden) {
                        val instance = engine.renderableManager.getInstance(entity)
                        if (instance != 0) engine.renderableManager.setLayerMask(instance, 0xff, 0)
                    }
                }
                asset.releaseSourceData()
            }
            // Publish the complete appearance at once, never a headless intermediate.
            models.forEach { scene!!.addEntities(it.entities) }
            // User-selected still portrait. Apply one native neutral pose, not a
            // moving idle or a gameplay pose; outfit changes still rebuild normally.
            models.forEach { if (it.instance.animator.animationCount > 0) it.instance.animator.applyAnimation(0, 0f) }
            models.forEach { it.instance.animator.updateBoneMatrices() }
            firstFrameLogged = false
            Log.info("[SMO] Preview ready: $appearance (${models.size} native parts)")
            schedule()
        } catch (error: Exception) { clearModels(engine); throw error }
    }

    private fun schedule() {
        if (!attached || !active || failed || framePosted || models.isEmpty() ||
            (portrait != null && !pending)) return
        framePosted = true; Choreographer.getInstance().postFrameCallback(this)
    }

    override fun doFrame(frameTimeNanos: Long) {
        framePosted = false
        if (!active || !attached || failed || models.isEmpty()) return
        val engine = e ?: return
        val render = renderer ?: return
        try {
            if (pending) {
                if (SystemClock.uptimeMillis() - pendingAt > 2000) error("Portrait readback timed out")
                if (render.beginFrame(chain!!,frameTimeNanos)) render.endFrame()
                engine.flush(); schedule(); return
            }
            if (portrait != null) return
            if (render.beginFrame(chain!!,frameTimeNanos)) {
                try {
                    val generation = token
                    val buffer = pixels!!.apply { clear() }
                    pending = true; pendingAt = SystemClock.uptimeMillis()
                    val descriptor = Texture.PixelBufferDescriptor(buffer,Texture.Format.RGBA,Texture.Type.UBYTE,
                        1,0,0,renderWidth,handler,Runnable {
                            pending = false
                            if (attached && active && generation == token) {
                                val bitmap = portrait ?: Bitmap.createBitmap(renderWidth,renderHeight,Bitmap.Config.ARGB_8888)
                                buffer.rewind(); bitmap.copyPixelsFromBuffer(buffer); portrait = bitmap
                                if (!firstFrameLogged) {
                                    firstFrameLogged = true
                                    var visibleSamples = 0
                                    for (y in 0 until renderHeight step 8) for (x in 0 until renderWidth step 8)
                                        if (bitmap.getPixel(x, y).ushr(24) > 8) visibleSamples++
                                    Log.info("[SMO] Preview first frame: view=${width}x$height visibleSamples=$visibleSamples appearance=$appearance")
                                }
                                postInvalidateOnAnimation()
                            }
                            schedule()
                        })
                    render.render(view!!)
                    render.readPixels(target!!,0,0,renderWidth,renderHeight,descriptor)
                } finally { render.endFrame() }
            }
            schedule()
        } catch (error: Exception) { fail("render",error) }
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        portrait?.let { canvas.drawBitmap(it,null,RectF(0f,0f,width.toFloat(),height.toFloat()),paint) }
    }

    private fun clearModels(engine: Engine) {
        models.asReversed().forEach { scene?.removeEntities(it.entities); loader?.destroyAsset(it) }
        models.clear()
    }

    private fun fail(where: String, error: Throwable) {
        Log.error("[SMO] Preview $where failed: ${error.message}")
        failed = true; portrait = null; invalidate()
        Choreographer.getInstance().removeFrameCallback(this); framePosted = false
    }

    override fun onDetachedFromWindow() {
        attached = false; ++token; active = false; loading = false
        Choreographer.getInstance().removeFrameCallback(this); framePosted = false
        io.shutdownNow()
        e?.let { engine ->
            runCatching {
                engine.flushAndWait(); clearModels(engine)
                view?.renderTarget = null
                target?.let(engine::destroyRenderTarget); color?.let(engine::destroyTexture); depth?.let(engine::destroyTexture)
                chain?.let(engine::destroySwapChain)
                resources?.destroy(); loader?.destroy(); provider?.destroyMaterials(); provider?.destroy()
                scene?.indirectLight = null; ambient?.let(engine::destroyIndirectLight)
                scene?.skybox = null; sky?.let(engine::destroySkybox)
                if (lightEntity != 0) { engine.destroyEntity(lightEntity); EntityManager.get().destroy(lightEntity) }
                if (cameraEntity != 0) { engine.destroyCameraComponent(cameraEntity); EntityManager.get().destroy(cameraEntity) }
                view?.let(engine::destroyView); scene?.let(engine::destroyScene); renderer?.let(engine::destroyRenderer)
                engine.destroy()
            }.onFailure { Log.warning("[SMO] Preview teardown: ${it.message}") }
        }
        e = null; portrait = null; pixels = null; pending = false
        super.onDetachedFromWindow()
    }
}
