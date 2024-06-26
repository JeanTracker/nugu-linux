# for TARGET_COMPILE_DEFINITIONS
SET(BUILTIN_PLUGIN_DEFS "")

# for TARGET_INCLUDE_DIRECTORIES
SET(BUILTIN_PLUGIN_IDIR "")

# for ADD_DEPENDENCIES
SET(BUILTIN_PLUGIN_DEPS "")

# for ADD_LIBRARY
SET(BUILTIN_PLUGIN_SRCS "")

# for pkg_check_modules
SET(BUILTIN_PLUGIN_PKGS "")

# shared library plugin list
SET(PLUGIN_LIST "")

# Macro for plugin definition
MACRO(DEFINE_PLUGIN name)
	STRING(TOUPPER ${name} NAME_UPPER)
	IF("${name}" IN_LIST BUILTIN_PLUGIN_LIST)
		SET(BUILTIN_PLUGIN_DEFS ${BUILTIN_PLUGIN_DEFS}
			-DNUGU_PLUGIN_BUILTIN_${NAME_UPPER})
		LIST(APPEND BUILTIN_PLUGIN_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/${name}.c)
	ELSE()
		LIST(APPEND PLUGIN_LIST ${name})
		ADD_LIBRARY(plg_${name} SHARED ${name}.c)
		TARGET_LINK_LIBRARIES(plg_${name} PRIVATE ${COMMON_LDFLAGS} libnugu)
		IF(IS_MINGW OR MSVC)
			SET_TARGET_PROPERTIES(plg_${name} PROPERTIES PREFIX "" OUTPUT_NAME nugu_plugin_${name})
			IF (IS_MINGW)
				SET_TARGET_PROPERTIES(plg_${name} PROPERTIES SUFFIX ".dll")
			ENDIF()
		ELSE()
			SET_TARGET_PROPERTIES(plg_${name} PROPERTIES PREFIX "" OUTPUT_NAME ${name})
		ENDIF()
		INSTALL(TARGETS plg_${name} LIBRARY DESTINATION ${plugindir} COMPONENT libnugu_component)
		ADD_DEPENDENCIES(plg_${name} libnugu)
	ENDIF()
ENDMACRO(DEFINE_PLUGIN)

DEFINE_PLUGIN(dummy)
DEFINE_PLUGIN(filedump)
DEFINE_PLUGIN(filereader)

# OPUS decoder plugin
IF(ENABLE_OPUS_PLUGIN)
	DEFINE_PLUGIN(opus)

	IF("opus" IN_LIST BUILTIN_PLUGIN_LIST)
		IF(ENABLE_BUILTIN_OPUS)
			# Use opus static library(externals/opus)
			SET(BUILTIN_PLUGIN_IDIR ${BUILTIN_PLUGIN_IDIR}
				${PROJECT_BINARY_DIR}/opus/include/opus)
			SET(BUILTIN_PLUGIN_LIBS ${BUILTIN_PLUGIN_LIBS}
				${PROJECT_BINARY_DIR}/opus/lib/libopus.a PARENT_SCOPE)
			LIST(APPEND BUILTIN_PLUGIN_DEPS OPUSLIB)
		ELSE()
			# Use opus shared library
			SET(BUILTIN_PLUGIN_IDIR ${BUILTIN_PLUGIN_IDIR} ${OPUS_INCLUDE_DIR})
			SET(BUILTIN_PLUGIN_LIBS ${BUILTIN_PLUGIN_LIBS} ${OPUS_LIBRARY} PARENT_SCOPE)
		ENDIF()
	ELSE()
		IF(ENABLE_BUILTIN_OPUS)
			# Use opus static library(externals/opus)
			TARGET_INCLUDE_DIRECTORIES(plg_opus PRIVATE
				${PROJECT_BINARY_DIR}/opus/include/opus)
			TARGET_LINK_LIBRARIES(plg_opus PRIVATE ${COMMON_LDFLAGS}
				${PROJECT_BINARY_DIR}/opus/lib/libopus.a
				libnugu -lm)
			ADD_DEPENDENCIES(plg_opus OPUSLIB)
		ELSE()
			# Use opus shared library
			TARGET_INCLUDE_DIRECTORIES(plg_opus PRIVATE ${OPUS_INCLUDE_DIR})
			TARGET_COMPILE_OPTIONS(plg_opus PRIVATE ${OPUS_CFLAGS})
			TARGET_LINK_LIBRARIES(plg_opus PRIVATE ${COMMON_LDFLAGS} ${OPUS_LIBRARY} libnugu)
		ENDIF()
	ENDIF()
ENDIF(ENABLE_OPUS_PLUGIN)

# OPUS encoder plugin
IF(ENABLE_OPUSENC_PLUGIN)
	DEFINE_PLUGIN(opus_encoder)

	IF("opus_encoder" IN_LIST BUILTIN_PLUGIN_LIST)
		SET(BUILTIN_PLUGIN_IDIR ${BUILTIN_PLUGIN_IDIR} ${OPUS_INCLUDE_DIR} ${OGG_INCLUDE_DIR})
		SET(BUILTIN_PLUGIN_LIBS ${BUILTIN_PLUGIN_LIBS} ${OPUS_LIBRARY} ${OGG_LIBRARY} PARENT_SCOPE)
	ELSE()
		TARGET_INCLUDE_DIRECTORIES(plg_opus_encoder PRIVATE ${OPUS_INCLUDE_DIR} ${OGG_INCLUDE_DIR})
		TARGET_COMPILE_OPTIONS(plg_opus_encoder PRIVATE ${OPUS_CFLAGS} ${OGG_CFLAGS})
		TARGET_LINK_LIBRARIES(plg_opus_encoder PRIVATE ${COMMON_LDFLAGS}
			${OPUS_LIBRARY} ${OGG_LIBRARY} libnugu)
	ENDIF()
ENDIF(ENABLE_OPUSENC_PLUGIN)

# PortAudio plugin - recorder, pcm
IF(ENABLE_PORTAUDIO_PLUGIN)
	SET(PORTAUDIO_BUILTIN 0)

	pkg_check_modules(portaudio REQUIRED portaudio-2.0 alsa)

	FOREACH(item portaudio portaudio_recorder portaudio_pcm_sync portaudio_pcm_async)
		DEFINE_PLUGIN(${item})
		IF("${item}" IN_LIST BUILTIN_PLUGIN_LIST)
			SET(PORTAUDIO_BUILTIN 1)
		ELSE()
			TARGET_COMPILE_OPTIONS(plg_${item} PRIVATE ${portaudio_CFLAGS})
			TARGET_LINK_LIBRARIES(plg_${item} PRIVATE ${COMMON_LDFLAGS} ${portaudio_LDFLAGS} libnugu)
		ENDIF()
	ENDFOREACH()

	IF(PORTAUDIO_BUILTIN)
		SET(BUILTIN_PLUGIN_PKGS ${BUILTIN_PLUGIN_PKGS} portaudio-2.0 alsa)
	ENDIF()
ENDIF(ENABLE_PORTAUDIO_PLUGIN)

# Gstreamer plugin - recorder, pcm, player
IF(ENABLE_GSTREAMER_PLUGIN)
	SET(GSTREAMER_BUILTIN 0)

	FOREACH(item gstreamer gstreamer_recorder gstreamer_pcm)
		DEFINE_PLUGIN(${item})
		IF("${item}" IN_LIST BUILTIN_PLUGIN_LIST)
			SET(GSTREAMER_BUILTIN 1)
		ELSE()
			TARGET_INCLUDE_DIRECTORIES(plg_${item} PRIVATE ${GST_INCLUDE_DIR})
			TARGET_LINK_LIBRARIES(plg_${item} PRIVATE ${COMMON_LDFLAGS}
				${GST_LIBRARY} libnugu)
			TARGET_COMPILE_OPTIONS(plg_${item} PRIVATE ${GST_CFLAGS})
		ENDIF()
	ENDFOREACH()

	IF(GSTREAMER_BUILTIN)
		SET(BUILTIN_PLUGIN_PKGS ${BUILTIN_PLUGIN_PKGS}
			gstreamer-1.0 gstreamer-app-1.0 gstreamer-pbutils-1.0)
	ENDIF()

ENDIF(ENABLE_GSTREAMER_PLUGIN)

# Speex encoder plugin
IF(ENABLE_SPEEX_PLUGIN AND ENABLE_VENDOR_LIBRARY)
	DEFINE_PLUGIN(speex)

	IF ("speex" IN_LIST BUILTIN_PLUGIN_LIST)
	ELSE()
		TARGET_LINK_LIBRARIES(plg_speex PRIVATE ${COMMON_LDFLAGS} ${vendor_pkgs_LDFLAGS} libnugu)
	ENDIF()
ENDIF()

# Plugins built into the SDK library.
IF(BUILTIN_PLUGIN_SRCS)
	ADD_LIBRARY(objbuiltin OBJECT ${BUILTIN_PLUGIN_SRCS})
	TARGET_COMPILE_DEFINITIONS(objbuiltin PRIVATE ${BUILTIN_PLUGIN_DEFS} -DNUGU_LIBRARY_BUILD)
	TARGET_INCLUDE_DIRECTORIES(objbuiltin PRIVATE ${BUILTIN_PLUGIN_IDIR})

	IF(BUILTIN_PLUGIN_PKGS)
		# Get CFLAGS and LDFLAGS from pkg-config list
		pkg_check_modules(builtin_plugin_pkgs REQUIRED ${BUILTIN_PLUGIN_PKGS})
		TARGET_COMPILE_OPTIONS(objbuiltin PRIVATE ${builtin_plugin_pkgs_CFLAGS})
	ENDIF()

	IF(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
		SET_TARGET_PROPERTIES(objbuiltin PROPERTIES
			COMPILE_PDB_NAME "objbuiltin"
			COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
	ENDIF()

	FOREACH(lib ${BUILTIN_PLUGIN_DEPS})
		ADD_DEPENDENCIES(objbuiltin ${lib})
	ENDFOREACH()

	SET(BUILTIN_PLUGIN_DEPS ${BUILTIN_PLUGIN_DEPS} PARENT_SCOPE)
ENDIF()

# output message
MESSAGE("-- Built-in plugin list: ${BUILTIN_PLUGIN_LIST}")
MESSAGE("-- Plugin list: ${PLUGIN_LIST}")
