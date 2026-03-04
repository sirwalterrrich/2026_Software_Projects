cmake_minimum_required(VERSION 3.10)

project(RobotConsole VERSION 0.1.0)

include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Robot Console")
set(VITA_TITLEID  "RIC000001")

add_executable(${PROJECT_NAME}
  source/main.c
  source/input.c
  source/network.c
  source/robot.c
  source/camera.c
  source/mjpeg.c
  source/log.c
  source/feedback.c
  source/ui.c
)

target_compile_definitions(${PROJECT_NAME} PRIVATE CURL_STATICLIB)

target_link_libraries(${PROJECT_NAME}
  curl
  ssl
  crypto
  z
  zstd
  vita2d
  jpeg
  png
  freetype
  bz2
  z
  SceCtrl_stub
  SceDisplay_stub
  SceGxm_stub
  ScePgf_stub
  SceNet_stub
  SceNetCtl_stub
  SceSysmodule_stub
  SceAppMgr_stub
  SceAppUtil_stub
  SceAudio_stub
  SceAudioIn_stub
  SceIofilemgr_stub
  SceCommonDialog_stub
  SceTouch_stub
  SceHid_stub
  SceMotion_stub
  ScePower_stub
  SceProcessmgr_stub
  SceRtc_stub
  SceSysmem_stub
  SceKernelThreadMgr_stub
  SceKernelModulemgr_stub
  SceLibKernel_stub
  m
)

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME})

vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID}
    ${PROJECT_NAME}.self
    VERSION 01.00
    NAME "${VITA_APP_NAME}"
    FILE assets/icon0.png sce_sys/icon0.png
         assets/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
         assets/sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
         assets/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
         assets/robots.json robots.json
         assets/camera.json camera.json
         assets/robots.txt robots.txt
         usr/local/vitasdk/arm-vita-eabi/ssl/openssl.cnf usr/local/vitasdk/arm-vita-eabi/ssl/openssl.cnf
)