cmake_minimum_required(VERSION 3.20)
project(SCSKiller LANGUAGES CXX ASM_MASM RC)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")  # statik CRT: oyunun yanına sevk edilecek ekstra bir şey yok

# d3d12.dll ve scskiller_warm.exe'nin Sürüm kaynağı (version.rc); build/publish.ps1 etiketin sürümünü iletir
set(SCSK_VERSION "0.0.0-internal.0" CACHE STRING "Yerel (native) sürüm kaynağındaki SemVer X.Y.Z[-pre]")
if(NOT SCSK_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-[0-9A-Za-z.-]+)?$")
  message(FATAL_ERROR "SCSK_VERSION X.Y.Z[-pre] formatında değil: ${SCSK_VERSION}")
endif()
set(SCSK_VERSION_MAJOR ${CMAKE_MATCH_1})
set(SCSK_VERSION_MINOR ${CMAKE_MATCH_2})
set(SCSK_VERSION_PATCH ${CMAKE_MATCH_3})
configure_file(version.h.in version.h @ONLY)

add_library(scskiller SHARED proxy.cpp warm11.cpp vtslots.cpp stubs.asm d3d12.def version.rc)
set_target_properties(scskiller PROPERTIES OUTPUT_NAME d3d12)
# /MAP: çökme (crash) yığınlarını okumak için fonksiyon RVA'ları. Yalnızca D3D11 ısıtmasının kullandığı (oyunda asla yüklenmez) ve
# yalnızca ilk cihazdaki kabulün (kaydedici klasörü) kullandığı bileşenleri gecikmeli yükle (Delay-load).
target_link_options(scskiller PRIVATE /MAP /DELAYLOAD:d3d11.dll /DELAYLOAD:d3dcompiler_47.dll /DELAYLOAD:dxgi.dll /DELAYLOAD:shell32.dll /DELAYLOAD:ole32.dll)
target_link_libraries(scskiller delayimp)
# Testler için hata enjeksiyonu: sürümü publish.ps1 tarafından ayarlanan etiketli bir sürümde asla bulunmaz
if(SCSK_VERSION MATCHES "^0[.]0[.]0-internal")
  target_compile_definitions(scskiller PRIVATE SCSK_TEST_HOOKS)
endif()

# scskiller_ags.dll: AGS'nin cihazı oluşturduğu AMD'nin AGS'sinin önündeki aynı kaydedici, oyunun amd_ags_x64.dll'i olarak kurulur 
# (uygulamanın GameFiles.AgsMakesTheDevice durumu). Oyunun kendi AGS'si amd_ags_real.dll olarak yeniden adlandırılır: Wrap_AgsCreateDevice
# bize aittir, ags_exports.txt içindeki diğer tüm dışa aktarımlar aynı listeden oluşturulan bir içe aktarma kütüphanesi aracılığıyla oraya gider.
file(STRINGS ags_exports.txt AGS_NAMES)
set(AGS_DEF ${CMAKE_CURRENT_BINARY_DIR}/ags_wrap.def)
set(AGS_REAL_DEF ${CMAKE_CURRENT_BINARY_DIR}/amd_ags_real.def)
set(AGS_REAL_LIB ${CMAKE_CURRENT_BINARY_DIR}/amd_ags_real.lib)
set(AGS_DEF_TEXT "EXPORTS
    agsDriverExtensionsDX12_CreateDevice = Wrap_AgsCreateDevice
    SCSKiller_StartWarm
")
set(AGS_REAL_DEF_TEXT "LIBRARY amd_ags_real.dll
EXPORTS
")
foreach(n ${AGS_NAMES})
  string(APPEND AGS_REAL_DEF_TEXT "    ${n}
")
  if(NOT n STREQUAL "agsDriverExtensionsDX12_CreateDevice")
    string(APPEND AGS_DEF_TEXT "    ${n}
")
  endif()
endforeach()
file(WRITE ${AGS_DEF} "${AGS_DEF_TEXT}")
file(WRITE ${AGS_REAL_DEF} "${AGS_REAL_DEF_TEXT}")
add_custom_command(OUTPUT ${AGS_REAL_LIB} COMMAND${CMAKE_AR} /nologo /DEF:${AGS_REAL_DEF} /MACHINE:X64 /OUT:${AGS_REAL_LIB}
                   DEPENDS ${AGS_REAL_DEF} ags_exports.txt VERBATIM)
add_custom_target(amd_ags_real_lib DEPENDS ${AGS_REAL_LIB})
add_library(scskiller_ags SHARED proxy.cpp warm11.cpp vtslots.cpp stubs.asm ${AGS_DEF} version.rc)
add_dependencies(scskiller_ags amd_ags_real_lib)
target_compile_definitions(scskiller_ags PRIVATE SCSK_AGS_WRAP)
target_include_directories(scskiller_ags PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_options(scskiller_ags PRIVATE /MAP /DELAYLOAD:d3d11.dll /DELAYLOAD:d3dcompiler_47.dll /DELAYLOAD:dxgi.dll /DELAYLOAD:shell32.dll /DELAYLOAD:ole32.dll)
target_link_libraries(scskiller_ags ${AGS_REAL_LIB} delayimp)

add_executable(selftest selftest.cpp)
target_link_libraries(selftest d3dcompiler)
target_include_directories(selftest PRIVATE third_party/vulkan)  # yalnızca başlıklar; vulkan-1.dll çalışma zamanında yüklenir
add_dependencies(selftest scskiller fakenext)

# Proxy'nin zincirlendiği bir modun `selftest chain` (scskiller.ini next=) için d3d12.dll'i; yalnızca test içindir, asla yayınlanmaz
add_library(fakenext SHARED fakenext.cpp fakenext.asm)

add_executable(scskiller_warm warm.cpp version.rc)
# Segment yığınındaki (warm.manifest) aynı exe, aynı dosya adı altında segheap\ içinde: ısıtıcı (warmer) bunu NVIDIA'da çalıştırır
add_executable(scskiller_warm_segheap warm.cpp version.rc warm.manifest)
set_target_properties(scskiller_warm_segheap PROPERTIES OUTPUT_NAME scskiller_warm
  RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/Release/segheap RUNTIME_OUTPUT_DIRECTORY_DEBUG${CMAKE_CURRENT_BINARY_DIR}/Debug/segheap
  # her iki exe de warm-host işaretçisini dışarı aktarır, bu nedenle her biri bir scskiller_warm.exp/.lib yazar; paralel derlemeler için onları ayrı tutun
  ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/Release/segheap ARCHIVE_OUTPUT_DIRECTORY_DEBUG${CMAKE_CURRENT_BINARY_DIR}/Debug/segheap)
target_include_directories(scskiller PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
foreach(t scskiller_warm scskiller_warm_segheap)
  target_compile_definitions(${t} PRIVATE SCSK_RC_WARM)
  target_include_directories(${t} PRIVATE${CMAKE_CURRENT_BINARY_DIR})
  add_dependencies(${t} scskiller)
endforeach()

# AMD GPU Servisleri (GPUOpen AGS SDK 6.3.1, MIT): scskiller_warm çalışma zamanında bunu yükler (--ags). AMD'nin imzalı sürüm DLL'i,
# hash ile sabitlenmiştir; o olmadan (çevrimdışı) AGS oyunlarının ısıtmaları basit bir cihaza geri döner.
set(AGS_DLL ${CMAKE_CURRENT_BINARY_DIR}/amd_ags_x64.dll)
set(AGS_SHA256 482B241EA855FDD59AAA7687C385CE4165B5340857E6A0E6D15BA27ECB65EE70)
if(EXISTS ${AGS_DLL})
  file(SHA256 ${AGS_DLL} AGS_HAVE)
  string(TOUPPER "${AGS_HAVE}" AGS_HAVE)
endif()
if(NOT AGS_HAVE STREQUAL AGS_SHA256)
  # EXPECTED_HASH yok: bununla birlikte başarısız bir indirme STATUS ne derse desin bir yapılandırma hatasıdır; hash aşağıda kontrol edilir
  file(DOWNLOAD https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/AGS_SDK/v6.3.1/ags_lib/lib/amd_ags_x64.dll ${AGS_DLL}.part
       TLS_VERIFY ON STATUS AGS_STATUS)
  list(GET AGS_STATUS 0 AGS_CODE)
  if(AGS_CODE EQUAL 0)
    file(SHA256 ${AGS_DLL}.part AGS_GOT)
    string(TOUPPER "${AGS_GOT}" AGS_GOT)
    if(NOT AGS_GOT STREQUAL AGS_SHA256)
      set(AGS_STATUS "SHA-256 ${AGS_GOT}, beklenen${AGS_SHA256} değil")
    endif()
  endif()
  if(AGS_CODE EQUAL 0 AND AGS_GOT STREQUAL AGS_SHA256)
    file(RENAME ${AGS_DLL}.part ${AGS_DLL})
  else()
    file(REMOVE ${AGS_DLL}.part ${AGS_DLL})
    message(WARNING "amd_ags_x64.dll indirilemedi: ${AGS_STATUS}")
  endif()
endif()
if(EXISTS ${AGS_DLL})
  add_custom_command(TARGET scskiller_warm POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${AGS_DLL}$<TARGET_FILE_DIR:scskiller_warm>)
endif()

# D3D11 sürücü önbelleği ölçüm probu. Bağımsızdır: SCSKiller shim'i dahil olmadan doğrudan sistem d3d11.dll'i ile konuşur.
# Aynı zamanda D3D11 ısıtma uçtan uca testinin (compile, first) "oyunudur".
add_executable(probe11 probe11.cpp)
target_link_libraries(probe11 d3dcompiler)

# D3D11 derleme anahtarı (compile-key) probu: Hangi giriş düzeni (input-layout) alanları, ortak aşamalar ve çıkış durumu sürücünün
# bir shader'ı yeniden derlemesine neden olur.
add_executable(layout11 layout11.cpp)
target_link_libraries(layout11 d3dcompiler)
