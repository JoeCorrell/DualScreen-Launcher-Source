package com.dualscreen.mods.botw
import com.dualscreen.companion.api.CompanionHost
internal class ModuleLog(private val host: CompanionHost) { fun info(s: String)=host.log(0,s); fun warning(s: String)=host.log(1,s); fun error(s: String)=host.log(2,s) }
