# apprenticeCAM - Qt 6 Widgets UI (formats/qt_port_plan.md, phase 1).
#   cmake -S viewer-qt -B viewer-qt/build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build viewer-qt/build -j4
# Third-party (LGPL-2.1, fetched at configure time, built as shared libraries next to the program):
#   SARibbon                       https://github.com/czyt1988/SARibbon                         tag v2.9.5
#   Qt-Advanced-Docking-System     https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System  tag v5.1.1
cmake_minimum_required(VERSION 3.21)
project(apprenticecam_qt LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Qt >= 6.6: SARibbon v2.9.5 uses QEvent::DevicePixelRatioChange (Qt 6.6). Debian 12 / Ubuntu 22.04 / 24.04 ship 6.2 - 6.4:
# their command-line tools build (viewer/Makefile), the Qt app does not.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Svg)
find_package(OpenGL REQUIRED COMPONENTS OpenGL EGL)
find_package(GLEW REQUIRED)
find_package(SQLite3 REQUIRED)

# ------------------------------------------------------------------ third party (pinned commits)
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
set(SARIBBON_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SARIBBON_BUILD_STATIC_LIBS OFF CACHE BOOL "" FORCE)
set(SARIBBON_USE_FRAMELESS_LIB OFF CACHE BOOL "" FORCE)
set(SARIBBON_INSTALL_IN_CURRENT_DIR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC OFF CACHE BOOL "" FORCE)
set(ADS_VERSION 5.1.1 CACHE STRING "" FORCE)
FetchContent_Declare(saribbon
  GIT_REPOSITORY https://github.com/czyt1988/SARibbon.git
  GIT_TAG        a21d30c2a8495da92c7db3adcdc124699b0f2a60   # v2.9.5
  GIT_SHALLOW    FALSE)
FetchContent_Declare(qtads
  GIT_REPOSITORY https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git
  GIT_TAG        023ce95934fecfd4cf5c672c9c404fabe0f54923   # v5.1.1
  GIT_SHALLOW    FALSE)
FetchContent_MakeAvailable(saribbon qtads)

# ------------------------------------------------------------------ the C core (viewer/*.c + viewer/core_ui; viewer/Makefile `core` compiles the same list)
set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../viewer)
set(CORE_C hmm_voronoi.c hmm_pipethin.c stockmodel.c cad_import.c stl_mesh.c dxf_read.c rpx.c scene.c mcam_load.c mcsurf.c mcsrfdl.c mcglhit.c mcsurf_build.c mcsrfint.c mcpartline.c mcpartline_mesh.c cam3d_mmodel.c mcsrfcrv.c mccsurf.c mcsurf_nrm.c mcspline.c mcspline_fit.c mcspline_simplify.c mcspline_chain.c mcspline_refit.c mcbiarc.c mcfilter.c mcfillet.c mcfillet_ent.c mcsurf_tonurbs.c part_tools.c xt_mesh.c cam2d.c cam2d_remach.c mcce_ctour.c mcce_dyn.c toollib.c toolprofile.c toolwiz.c
           feedspeed.c backplot.c mcam_write.c mcam_saveops.c geo2d.c solid25.c cam_fileop.c nc_post.c nci_reverse.c cam_postvals.c
           area_mill.c verify.c nci_write.c bnci_update.c draft.c draft_create.c draft_create_circ.c draft_create_ang.c draft_create_tan.c draft_create_pt.c draft_ord.c draft_pick.c draft_note.c engrave.c lathe_face.c lathe_stock.c lathe_tool.c lathe_axis.c lathe_newop.c sldtoolp.c)
list(APPEND CORE_C mcsrfext.c)   # Surfaces > Extend: extend_surf's NURBS extension
list(APPEND CORE_C mcsrftrim.c)   # Surfaces > Trim: the face trimming core (MaFace::TrimToLoopsNew, MaTrimSurfToPlane), formats/surfaces_trim_decode.md
list(TRANSFORM CORE_C PREPEND ${CORE}/)
# viewer/core_ui: the UI behaviour moved out of the removed mcamui.c (document, selection, picking, AutoCursor, overlay, undo; phase 2; wireframe commands + panel model, phase 3; chaining, phase 4; Extrude, phase 5; operations + toolpath dialog, phase 6)
set(CORE_UI_C cui_srfdl.c cui_srfedit.c cui_cedge.c cui_slice.c cui_plsel.c cui_srfint.c cui_csurf.c cui_partline.c cui_dsurf.c cui_spline.c cui_surf.c cui_attr.c cui_doc.c cui_feat.c cui_geo.c cui_wfmisc.c cui_xform.c cui_xdyn.c cui_xroll.c cui_sel.c cui_snap.c cui_overlay.c cui_undo.c cui_chain.c cui_sldchain.c cui_extrude.c
              cui_ops.c cui_opdlg.c cui_toolpage.c cui_optree.c cui_toolmgr.c cui_toolwiz.c
              cui_backplot.c cui_verify.c cui_analyze.c cui_ecp.c cui_lface.c cui_misc.c   # phase 8
              cui_stock.c cui_bbox.c cui_planes.c cui_grpprops.c         # Stock Setup, Bounding Box panel, planes, Files / Tool Settings
              cui_dirty.c                                                # dirty operations (Regenerate, dialog 977)
              cui_defaults.c cui_preview.c                               # Q19 Save parameters to defaults file, Q20 Preview toolpath
              cui_draft.c cui_draft_ord.c cui_draft_pick.c cui_draft_dim.c cui_draft_ang.c cui_draft_base.c cui_draft_tan.c cui_draft_pt.c cui_draft_note.c cui_note.c   # notes (E_NOTE); the Drafting tab's dimensions / ordinates
              cui_nest.c)                                                # Transform > Geometry Nesting (NESTING.DLL's geometry flow)
list(APPEND CORE_UI_C cui_srftrim.c)   # Surfaces > Modify: Extend (Trim / Extend Trimmed Edges decoded, greyed)
list(TRANSFORM CORE_UI_C PREPEND ${CORE}/core_ui/)
list(APPEND CORE_C ${CORE_UI_C})
# viewer/mp: the MP post engine (Mastercam .pst posts; formats/mp_engine.md), used by the Post when the machine group's
# "Post with Mastercam post (.pst)" is on (core_ui cui_ops.c post_ops; ours, user decision P2). Built with viewer/mp/Makefile's
# flags (its own warning level); viewer/Makefile `core` compiles the same list.
set(MP_C mp_post.c nci_ascii.c mp_parse.c mp_parse_tables.c mp_run.c mp_eval.c mp_eval_fn.c mp_out.c mp_fmt.c mp_nciloop.c
         mp_run_prm.c mp_prm.c)
list(TRANSFORM MP_C PREPEND ${CORE}/mp/)
# viewer/nest: NestLib's Rectangular engine (bit-exact port of n64dll.dll; viewer/nest/Makefile's flags: no FMA contraction)
set(NEST_C ${CORE}/nest/nest_rect.c ${CORE}/nest/nest_rect_job.c ${CORE}/nest/nest_job_io.c)   # + nest_rect_job.c: Geometry Nesting's Rectangular back-end (nest_job.h)
# + NestLib's TrueShape engine (viewer/nest/nest_ts.c + ts/: transliterated n64dll.dll) and its Geometry Nesting back-end
# nest_ts_job.c (nest_engine_ts). viewer/nest/Makefile's flags: the generated ts/build/ts_gen.c / ts_data.c with TSGENFLAGS,
# nest_ts*.c permissive (they include the generated header), ts/*.c with CFLAGS
set(NEST_TS_C ${CORE}/nest/ts/ts_rt.c ${CORE}/nest/ts/ts_crt.c ${CORE}/nest/ts/ts_printf.c ${CORE}/nest/ts/ts_libm.c)
set(NEST_TS_PERM_C ${CORE}/nest/nest_ts.c ${CORE}/nest/nest_ts_job.c)
set(NEST_TS_GEN_C ${CORE}/nest/ts/build/ts_gen.c ${CORE}/nest/ts/build/ts_data.c)
add_library(mcamcore STATIC ${CORE_C} ${MP_C} ${NEST_C} ${NEST_TS_C} ${NEST_TS_PERM_C} ${NEST_TS_GEN_C} ${CORE}/offset.cpp ${CORE}/vendor/clipper6/clipper.cpp)
set_source_files_properties(${NEST_C} ${NEST_TS_C} PROPERTIES COMPILE_OPTIONS "-O2;-Wall;-Wno-unused-parameter;-ffp-contract=off;-fno-fast-math")
set_source_files_properties(${NEST_TS_PERM_C} PROPERTIES COMPILE_OPTIONS "-O2;-std=gnu17;-fpermissive;-w;-ffp-contract=off;-fno-fast-math")
set_source_files_properties(${NEST_TS_GEN_C} PROPERTIES COMPILE_OPTIONS
  "-O1;-std=gnu17;-fpermissive;-w;-fwrapv;-fno-strict-aliasing;-ffp-contract=off;-fno-fast-math")
set_source_files_properties(${MP_C} PROPERTIES COMPILE_OPTIONS "-std=gnu11;-Wall")
target_include_directories(mcamcore PUBLIC ${CORE})
# the Makefile's flags (CFLAGS + its CORE_FLAGS -Wno-*; formerly the mcamui target's); C++ parts are built with -O2 only there
set_source_files_properties(${CORE_C} PROPERTIES COMPILE_OPTIONS
  "-Wall;-Wextra;-Wno-misleading-indentation;-Wno-unused-parameter;-Wno-sign-compare;-Wno-unused-function;-Wno-format-truncation")
target_compile_options(mcamcore PRIVATE -O2)
set_target_properties(mcamcore PROPERTIES CXX_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
# SQLite's imported target: CMake's FindSQLite3 makes SQLite3::SQLite3; a distribution's own SQLite config (Fedora 43) makes
# SQLite::SQLite3; else the plain variables
if (TARGET SQLite3::SQLite3)
  set(SQLITE_LIB SQLite3::SQLite3)
elseif (TARGET SQLite::SQLite3)
  set(SQLITE_LIB SQLite::SQLite3)
else()
  set(SQLITE_LIB ${SQLite3_LIBRARIES})
  target_include_directories(mcamcore PUBLIC ${SQLite3_INCLUDE_DIRS})
endif()
target_link_libraries(mcamcore PUBLIC GLEW::GLEW OpenGL::GL ${SQLITE_LIB} m)
# 3drough (2026-10-24): the 3D toolpaths' drive mesh (viewer/occt_mesh.cpp: XT -> OpenCASCADE (viewer/occt/xt_occt.cpp) -> BRepMesh,
# formats/hst3d_port_plan.md 6). With OCCT's headers found: HAVE_OCCT and its toolkits; without them occt_mesh.cpp is a stub and the
# 3D ops refuse ("built without OpenCASCADE"). viewer/Makefile does the same detection.
find_path(OCCT_INCLUDE_DIR BRepMesh_IncrementalMesh.hxx PATHS /usr/include/opencascade /usr/local/include/opencascade)
target_sources(mcamcore PRIVATE ${CORE}/occt_mesh.cpp ${CORE}/occt_prim.cpp ${CORE}/occt_cad.cpp ${CORE}/occt_edges.cpp)   # occt_prim: Surfaces > Simple (decision S-1)
if (OCCT_INCLUDE_DIR)
  target_sources(mcamcore PRIVATE ${CORE}/occt/xt_occt.cpp)
  set_source_files_properties(${CORE}/occt_mesh.cpp ${CORE}/occt_prim.cpp ${CORE}/occt_cad.cpp ${CORE}/occt_edges.cpp ${CORE}/occt/xt_occt.cpp PROPERTIES COMPILE_OPTIONS "-std=c++17")
  target_compile_definitions(mcamcore PRIVATE HAVE_OCCT)
  target_include_directories(mcamcore PRIVATE ${OCCT_INCLUDE_DIR})
  # OCCT >= 7.8 has the STEP / IGES readers in TKDESTEP / TKDEIGES; 7.6 / 7.7 (Debian 12, Ubuntu 22.04 / 24.04) in TKSTEP* / TKIGES
  find_library(OCCT_TKDESTEP TKDESTEP)
  if (OCCT_TKDESTEP)
    set(OCCT_DE TKDESTEP TKDEIGES)
  else()
    set(OCCT_DE TKSTEP TKSTEP209 TKSTEPAttr TKSTEPBase TKIGES)
  endif()
  target_link_libraries(mcamcore PUBLIC ${OCCT_DE} TKXSBase TKBO TKBool TKMesh TKShHealing TKPrim TKTopAlgo TKGeomAlgo TKBRep TKGeomBase TKG3d TKG2d TKMath TKernel)
endif()

# ------------------------------------------------------------------ the program
add_executable(apprenticecam
  src/main.cpp src/mainwindow.cpp src/mainwindow.h src/uilayout.cpp src/uilayout.h src/icons.cpp src/icons.h
  src/viewwidget.cpp src/viewwidget.h src/managers.cpp src/managers.h src/backstage.cpp src/backstage.h
  src/testhooks.cpp src/testhooks.h src/core_bridge.c src/core_bridge.h
  src/geopanel.cpp src/geopanel.h src/acsettings.cpp src/acsettings.h src/chaindlg.cpp src/chaindlg.h src/expanel.cpp src/expanel.h
  src/opdlg.cpp src/opdlg.h src/toolmgr.cpp src/toolmgr.h src/toolwiz.cpp src/toolwiz.h
  src/bpdlg.cpp src/bpdlg.h src/simwin.cpp src/simwin.h src/dialogs8.cpp src/dialogs8.h src/homedlg.cpp src/homedlg.h src/stockdlg.cpp src/stockdlg.h src/nestdlg.cpp src/nestdlg.h src/lfacedlg.cpp src/lfacedlg.h src/plseldlg.cpp src/plseldlg.h)
target_sources(apprenticecam PRIVATE src/textfit.cpp src/textfit.h)   # ours: text fit fixes + the MCAMUI_TEXTCHECK checker
set_source_files_properties(src/core_bridge.c PROPERTIES COMPILE_OPTIONS
  "-Wall;-Wextra;-Wno-unused-parameter;-Wno-sign-compare;-Wno-unused-function;-Wno-format-truncation")
target_link_libraries(apprenticecam PRIVATE mcamcore Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Svg
  SARibbonBar ads::qtadvanceddocking OpenGL::EGL)
target_sources(apprenticecam PRIVATE src/mcpserver.cpp src/mcpserver.h)   # ours: the MCP control socket (--mcp, tools/mcp_apprenticecam.py)
# the shipped layout files (viewer/ui: layout.txt, cmd_labels.tsv, icon_map.tsv) are found as <exe dir>/ui
add_custom_command(TARGET apprenticecam POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CORE}/ui $<TARGET_FILE_DIR:apprenticecam>/ui)
# the dialog templates' font (user decision UI-3; viewer-qt/fonts/README.md: Wine's Tahoma, LGPL-2.1-or-later) as <exe dir>/fonts
add_custom_command(TARGET apprenticecam POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/fonts $<TARGET_FILE_DIR:apprenticecam>/fonts)
# the fallback icons (ours, user decision I-1: FreeCAD's LGPL icons, tools/oss_icons.py) as <exe dir>/icons_oss
add_custom_command(TARGET apprenticecam POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/icons_oss $<TARGET_FILE_DIR:apprenticecam>/icons_oss)
