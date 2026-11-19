CFLAGS ?= -O2 -Wall -Wextra -Wno-misleading-indentation
SDL = $$(sdl2-config --cflags --libs)

# The GUI is the Qt app (../viewer-qt, CMake), which compiles the shared C core of this folder itself. The nuklear apps
# (mcamui, mcamgl) were removed on 2026-10-22 (formats/qt_port_plan.md phase 9; their sources are in
# ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz). `core` compiles every core source the Qt app uses with the
# flags it uses (a warnings check without CMake); it links nothing.
# mcamview (the old SDL2 viewer) only when SDL2 is installed (sdl2-config); everything else needs no SDL2.
HAVE_SDL2 := $(shell command -v sdl2-config >/dev/null 2>&1 && echo 1)
all: $(if $(HAVE_SDL2),mcamview) xt2obj cam_cli tool_cli nc_dev core ifk_cli ifk_stl_port

mcamview: mcamview.c
	$(CC) $(CFLAGS) -o $@ mcamview.c $(SDL) -lm

offset.o: offset.cpp offset.h
	$(CXX) -O2 -c offset.cpp -o $@

clipper6.o: vendor/clipper6/clipper.cpp
	$(CXX) -O2 -c $< -o $@

# 3drough (2026-10-24): the 3D toolpaths' drive mesh = the XT -> OpenCASCADE translator (occt/xt_occt.cpp) + BRepMesh
# (occt_mesh.cpp, formats/hst3d_port_plan.md 6). Built with OCCT when its headers are at OCC_INC; without them occt_mesh.o
# is a stub and the 3D ops refuse ("built without OpenCASCADE"). viewer-qt/CMakeLists.txt does the same detection.
OCC_INC ?= /usr/include/opencascade
HAVE_OCCT := $(shell test -f $(OCC_INC)/BRepMesh_IncrementalMesh.hxx && echo 1)
ifeq ($(HAVE_OCCT),1)
OCCT_FLAGS = -DHAVE_OCCT -I$(OCC_INC)
OCCT_OBJS = occt_mesh.o occt_prim.o occt_cad.o occt_edges.o xt_occt.o occt_xt.o occt_solid.o
# OCCT >= 7.8: STEP / IGES in TKDESTEP / TKDEIGES; 7.6 / 7.7 (Debian 12, Ubuntu 22.04 / 24.04): TKSTEP* / TKIGES
OCCT_DE := $(if $(filter /%,$(shell $(CXX) -print-file-name=libTKDESTEP.so)),-lTKDESTEP -lTKDEIGES,-lTKSTEP -lTKSTEP209 -lTKSTEPAttr -lTKSTEPBase -lTKIGES)
OCCT_LIBS = $(OCCT_DE) -lTKXSBase -lTKOffset -lTKFillet -lTKBO -lTKBool -lTKMesh -lTKShHealing -lTKPrim -lTKTopAlgo -lTKGeomAlgo -lTKBRep -lTKGeomBase -lTKG3d -lTKG2d -lTKMath -lTKernel
else
OCCT_OBJS = occt_mesh.o occt_prim.o occt_cad.o occt_edges.o occt_solid.o
endif
occt_mesh.o: occt_mesh.cpp occt_mesh.h occt/xt_occt.h xt_mesh.h
	$(CXX) -O2 -g -std=c++17 $(OCCT_FLAGS) -I. -c occt_mesh.cpp -o $@
occt_edges.o: occt_edges.cpp occt_cad.h occt_prim.h occt/xt_occt.h
	$(CXX) -O2 -g -std=c++17 $(OCCT_FLAGS) -c occt_edges.cpp -o $@

occt_cad.o: occt_cad.cpp occt_cad.h occt_prim.h
	$(CXX) -O2 -g -std=c++17 $(OCCT_FLAGS) -I. -c occt_cad.cpp -o $@
occt_prim.o: occt_prim.cpp occt_prim.h
	$(CXX) -O2 -g -std=c++17 $(OCCT_FLAGS) -I. -c occt_prim.cpp -o $@
xt_occt.o: occt/xt_occt.cpp occt/xt_occt.h
	$(CXX) -O2 -g -std=c++17 $(OCCT_FLAGS) -c occt/xt_occt.cpp -o $@
# SOL-1 / SOL-3 (2026-11-18): the OCCT -> XT writer and the Extrude kernel on OCCT (formats/occt_xt.md, formats/extrude.md)
occt_xt.o: occt/occt_xt.cpp occt/occt_xt.h occt/xt_refgeom.h occt/xt_occt.h
	$(CXX) -O2 -g -std=c++17 $(OCCT_FLAGS) -c occt/occt_xt.cpp -o $@
occt_solid.o: occt_solid.cpp occt_solid.h occt/occt_xt.h occt/xt_occt.h xt_mesh.h
	$(CXX) -O2 -g -std=c++17 $(OCCT_FLAGS) -I. -c occt_solid.cpp -o $@

# the core sources (viewer-qt/CMakeLists.txt CORE_C / CORE_UI_C)
CORE_C = hmm_voronoi.c hmm_pipethin.c stockmodel.c cad_import.c stl_mesh.c dxf_read.c rpx.c scene.c mcam_load.c mcsurf.c mcsrfdl.c mcglhit.c mcsurf_build.c mcsrfint.c mcpartline.c mcpartline_mesh.c cam3d_mmodel.c mcsrfcrv.c mccsurf.c mcsurf_nrm.c mcspline.c mcspline_fit.c mcspline_simplify.c mcspline_chain.c mcspline_refit.c mcbiarc.c mcfilter.c mcfillet.c mcfillet_ent.c mcsurf_tonurbs.c part_tools.c xt_mesh.c cam2d.c cam2d_remach.c mcce_ctour.c mcce_dyn.c toollib.c toolprofile.c toolwiz.c feedspeed.c \
         backplot.c mcam_write.c mcam_saveops.c sld_ops.c geo2d.c solid25.c cam_fileop.c nc_post.c nci_reverse.c cam_postvals.c area_mill.c verify.c nci_write.c sldtoolp.c
CORE_UI = cui_srfdl cui_srfedit cui_cedge cui_slice cui_plsel cui_srfint cui_csurf cui_partline cui_dsurf cui_spline cui_surf cui_attr cui_doc cui_geo cui_wfmisc cui_xform cui_xdyn cui_xroll cui_feat cui_sldsave cui_sel cui_snap cui_overlay cui_undo cui_chain cui_sldchain cui_extrude cui_fillet cui_ops cui_opdlg \
          cui_toolpage cui_optree cui_toolmgr cui_toolwiz cui_backplot cui_verify cui_analyze cui_ecp cui_lface cui_misc cui_stock cui_bbox cui_planes cui_grpprops \
          cui_dirty cui_defaults cui_preview cui_draft cui_draft_ord cui_draft_pick cui_draft_dim cui_draft_ang cui_draft_base cui_draft_tan cui_draft_pt cui_draft_note cui_note cui_nest
CORE_C += bnci_update.c draft.c draft_create.c draft_create_circ.c draft_create_ang.c draft_create_tan.c draft_create_pt.c draft_ord.c draft_pick.c draft_note.c engrave.c lathe_face.c lathe_stock.c lathe_tool.c lathe_axis.c lathe_newop.c   # lathe_newop: a new Lathe Face (menu / ribbon); lathe_axis: lathe axis combinations + default Tplane; lathe_tool: lathe tools (Make_op_tool); lathe_face: Lathe Face engine (cam2d.c gen_lathe_face); lathe_stock: lathe stock boundaries (Face from stock); draft_note: notes (record, creation API); draft_create*: the Drafting tab's dimension creation (linear, circular, angular, tangent / perpendicular, point); draft_ord: ordinate; draft_pick: picking drafting entities; engrave: Engrave engine (cam2d_engrave.c); D2 (BnciUpdate NCI patch), drafting display
CORE_C += mcsrfext.c   # Surfaces > Extend: extend_surf's NURBS extension (formats/surfaces_tab_plan.md "E. Extend")
CORE_C += mcsrftrim.c   # Surfaces > Trim: the face trimming core (MaFace::TrimToLoopsNew, MaTrimSurfToPlane), formats/surfaces_trim_decode.md
CORE_UI += cui_srftrim   # Surfaces > Modify: Extend (Trim / Extend Trimmed Edges decoded, greyed)
CORE_FLAGS = -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function -Wno-format-truncation -I.
# viewer/mp (the MP post engine, viewer-qt/CMakeLists.txt MP_C), with viewer/mp/Makefile's flags
CORE_MP = mp_post nci_ascii mp_parse mp_parse_tables mp_run mp_eval mp_eval_fn mp_out mp_fmt mp_nciloop mp_run_prm mp_prm
NEST_TS = nest/ts/ts_rt.c nest/ts/ts_crt.c nest/ts/ts_printf.c nest/ts/ts_libm.c nest/nest_ts.c nest/nest_ts_job.c nest/ts/build/ts_gen.c nest/ts/build/ts_data.c
core: $(addprefix core_obj/,$(CORE_C:.c=.o) $(addsuffix .o,$(CORE_UI))) $(addprefix core_obj/mp_,$(addsuffix .o,$(CORE_MP))) core_obj/nest_rect.o core_obj/nest_rect_job.o core_obj/nest_job_io.o \
      $(addprefix core_obj/,$(notdir $(NEST_TS:.c=.o)))
# viewer/nest: NestLib's Rectangular and TrueShape engines (viewer-qt/CMakeLists.txt NEST_C / NEST_TS*), with viewer/nest/Makefile's
# flags (no FMA contraction; the generated ts/build/*.c with its TSGENFLAGS, nest_ts*.c permissive)
core_obj/ts_%.o: nest/ts/ts_%.c $(wildcard nest/ts/*.h)
	@mkdir -p core_obj
	$(CC) -O2 -g -Wall -Wno-unused-parameter -ffp-contract=off -fno-fast-math -c $< -o $@
core_obj/nest_ts.o core_obj/nest_ts_job.o: core_obj/%.o: nest/%.c nest/nest_ts.h nest/nest_job.h nest/ts/build/ts_gen.h
	@mkdir -p core_obj
	$(CC) -O2 -g -std=gnu17 -fpermissive -w -ffp-contract=off -fno-fast-math -c $< -o $@
core_obj/ts_gen.o core_obj/ts_data.o: core_obj/%.o: nest/ts/build/%.c nest/ts/build/ts_gen.h
	@mkdir -p core_obj
	$(CC) -O1 -g -std=gnu17 -fpermissive -w -fwrapv -fno-strict-aliasing -ffp-contract=off -fno-fast-math -c $< -o $@
core_obj/nest_rect.o: nest/nest_rect.c nest/nest_rect.h
	@mkdir -p core_obj
	$(CC) -O2 -g -Wall -ffp-contract=off -fno-fast-math -c $< -o $@
core_obj/nest_rect_job.o: nest/nest_rect_job.c nest/nest_rect.h nest/nest_job.h
	@mkdir -p core_obj
	$(CC) -O2 -g -Wall -c $< -o $@
core_obj/nest_job_io.o: nest/nest_job_io.c nest/nest_job_io.h nest/nest_job.h
	@mkdir -p core_obj
	$(CC) -O2 -g -Wall -c $< -o $@
core_obj/mp_%.o: mp/%.c $(wildcard mp/*.h) mcam_load.h
	@mkdir -p core_obj
	$(CC) -O2 -g -Wall -std=gnu11 -Imp -I. -c $< -o $@
core_obj/mp_mp_nciloop.o: mp/mp_nciloop_tt.c mp/mp_nciloop_sub.c
core_obj/%.o: %.c $(wildcard *.h) $(wildcard mcpartline_trim*.inc) core_ui/core_ui.h
	@mkdir -p core_obj
	$(CC) $(CFLAGS) $(CORE_FLAGS) -c $< -o $@
core_obj/mcce_dyn.o: mcce_medax.c mcce_medax_par.c mcce_medent.c mcce_trace.c
core_obj/%.o: core_ui/%.c $(wildcard core_ui/*.h core_ui/*.inc *.h) optree_text.inc
	@mkdir -p core_obj
	$(CC) $(CFLAGS) $(CORE_FLAGS) -c $< -o $@
core_obj/cam2d.o: cam2d_remach_gen.c cam2d_remach.h cam2d_ops.c cam2d_dyn.c cam2d_area.c cam2d_pock.c cam2d_pock_entry.c cam2d_pock_pspiral.c cam2d_pock_tspiral.c cam2d_pock_ovlp.c cam2d_pock_isl.c cam2d_pock_zz.c cam2d_pock_medax.c cam2d_pock_rem.c cam2d_pock_taper.c cam2d_pock_open.c cam2d_pock_open_k.c cam2d_pock_fin.c cam2d_pock_hsx.c cam2d_face.c cam2d_slot_arc.c cam3d_rough.c cam3d_finish.c cam3d_checktrim.c cam3d_hstlink.c cam3d_pencil.c cam3d_pencil.h cam3d_horiz.c cam3d_horiz.h cam3d_blend.c cam3d_blend.h cam3d_srough.c pgeo.c   # included by cam2d.c cam2d_engrave.c cam_smooth.c

cam_cli: cam_cli.c cam2d.c stockmodel.c stockmodel.h cam2d.h cam2d_remach.c cam2d_remach_gen.c cam2d_remach.h cam2d_ops.c cam2d_dyn.c cam2d_area.c cam2d_pock.c cam2d_pock_entry.c cam2d_pock_pspiral.c cam2d_pock_tspiral.c cam2d_pock_ovlp.c cam2d_pock_isl.c cam2d_pock_zz.c cam2d_pock_medax.c cam2d_pock_rem.c cam2d_pock_taper.c cam2d_pock_open.c cam2d_pock_open_k.c cam2d_pock_fin.c cam2d_pock_hsx.c cam2d_face.c cam2d_slot_arc.c cam3d_rough.c cam3d_finish.c cam3d_checktrim.c cam3d_hstlink.c cam3d_pencil.c cam3d_pencil.h cam3d_horiz.c cam3d_horiz.h cam3d_blend.c cam3d_blend.h cam3d_srough.c cam3d_mmodel.c cam3d_mmodel.h mcpartline_mesh.c mcpartline.h mcsurf_nrm.c hmm_voronoi.c hmm_voronoi.h hmm_pipethin.c hmm_pipethin.h pgeo.c pgeo.h mcce_dyn.c mcce_dyn.h mcce_medax.c mcce_medax_par.c mcce_trace.c mcam_load.c mcspline.c part_tools.c part_tools.h toollib.c toollib.h toolprofile.c toolprofile.h feedspeed.c feedspeed.h feedspeed.c feedspeed.h backplot.c backplot.h mcam_write.c mcam_saveops.c xt_mesh.c xt_mesh.h cam_fileop.c sldtoolp.c sldtoolp.h cam_fileop.h nc_post.c nc_post.h nci_reverse.c nci_reverse.h cam_postvals.c cam_postvals.h area_mill.c area_mill.h verify.c verify.h nci_write.c nci_write.h offset.o clipper6.o $(OCCT_OBJS) occt_mesh.h paths.h mcce_medent.c engrave.c engrave.h cam2d_engrave.c cam_smooth.c mcce_ctour.c mcce_ctour.h mcce_sobool.inc mcce_offset.inc lathe_face.c lathe_face.h lathe_stock.c lathe_stock.h lathe_tool.c lathe_tool.h lathe_axis.c lathe_axis.h lathe_newop.c lathe_newop.h
	$(CC) $(CFLAGS) -o cam_cli.o -c cam_cli.c && $(CC) $(CFLAGS) -c stockmodel.c cam2d.c cam3d_mmodel.c mcpartline_mesh.c mcsurf_nrm.c hmm_voronoi.c hmm_pipethin.c cam2d_remach.c mcce_ctour.c mcce_dyn.c mcam_load.c mcsurf.c mcspline.c part_tools.c toollib.c toolprofile.c feedspeed.c backplot.c mcam_write.c mcam_saveops.c sld_ops.c xt_mesh.c cam_fileop.c sldtoolp.c nc_post.c nci_reverse.c cam_postvals.c area_mill.c verify.c nci_write.c engrave.c lathe_face.c lathe_stock.c lathe_tool.c lathe_axis.c lathe_newop.c
	$(CXX) -o $@ cam_cli.o stockmodel.o cam2d.o cam3d_mmodel.o mcpartline_mesh.o mcsurf_nrm.o hmm_voronoi.o hmm_pipethin.o cam2d_remach.o mcce_ctour.o mcce_dyn.o mcam_load.o mcsurf.o mcspline.o part_tools.o toollib.o toolprofile.o feedspeed.o backplot.o mcam_write.o mcam_saveops.o sld_ops.o xt_mesh.o cam_fileop.o sldtoolp.o nc_post.o nci_reverse.o cam_postvals.o area_mill.o verify.o nci_write.o engrave.o lathe_face.o lathe_stock.o lathe_tool.o lathe_axis.o lathe_newop.o offset.o clipper6.o $(OCCT_OBJS) $(OCCT_LIBS) -lsqlite3 -lm

tool_cli: tool_cli.c toollib.c toollib.h toolprofile.c mcam_load.c mcam_load.h part_tools.c part_tools.h paths.h
	$(CC) $(CFLAGS) -o $@ tool_cli.c toollib.c toolprofile.c mcam_load.c mcspline.c part_tools.c -lsqlite3 -lm

# Turn Profile kernel port (Interfacial IFSpinsil, formats/interfacial.md 6-7); tools/tests/ifk_spinsil_test.sh
# Silhouette Boundary kernel end to end (interfacial.md 11.9): ifk_silhouette.c + its stages
IFK_SIL_SRC = ifk/ifk_silhouette.c ifk/ifk_silhouette_in.c ifk/ifk_planecut.c ifk/ifk_sil_classify.c ifk/ifk_sil_shell2d.c
IFK_SIL_HDR = ifk/ifk_silhouette.h ifk/ifk_silhouette_in.h ifk/ifk_planecut.h ifk/ifk_sil_classify.h ifk/ifk_sil_shell2d.h
ifk_cli: ifk/ifk_cli.c ifk/ifk_spinsil.c ifk/ifk_spinsil.h ifk/ifk_curvexfer.c ifk/ifk_curvexfer.h ifk/ifk_mcside.c ifk/ifk_mcside.h $(IFK_SIL_SRC) $(IFK_SIL_HDR)
	$(CC) $(CFLAGS) -o $@ ifk/ifk_cli.c ifk/ifk_spinsil.c ifk/ifk_curvexfer.c ifk/ifk_mcside.c $(IFK_SIL_SRC) -lm
ifk_silrun_test: ifk/ifk_silrun_test.c ifk/ifk_curvexfer.c ifk/ifk_curvexfer.h ifk/ifk_spinsil.h $(IFK_SIL_SRC) $(IFK_SIL_HDR)
	$(CC) $(CFLAGS) -o $@ ifk/ifk_silrun_test.c ifk/ifk_curvexfer.c $(IFK_SIL_SRC) -lm
# Silhouette Boundary input stage (interfacial.md 11) and the mcmsurf HBE filter (interfacial.md 10.5): self-test drivers
ifk_sil_test: ifk/ifk_sil_test.c ifk/ifk_silhouette_in.c ifk/ifk_silhouette_in.h
	$(CC) $(CFLAGS) -o $@ ifk/ifk_sil_test.c ifk/ifk_silhouette_in.c -lm
ifk_planecut_test: ifk/ifk_planecut_test.c ifk/ifk_planecut.c ifk/ifk_planecut.h ifk/ifk_silhouette_in.c ifk/ifk_silhouette_in.h ifk/ifk_sil_classify.c ifk/ifk_sil_classify.h ifk/ifk_sil_shell2d.c ifk/ifk_sil_shell2d.h
	$(CC) $(CFLAGS) -o $@ ifk/ifk_planecut_test.c ifk/ifk_planecut.c ifk/ifk_silhouette_in.c ifk/ifk_sil_classify.c ifk/ifk_sil_shell2d.c -lm
# HBE records -> curves and chains (mcmsurf FUN_180232540, interfacial.md 14.2); tools/tests/ifk_hbechain_test.sh, ifk_hbechain_oracle.sh
IFK_HBECHAIN_SRC = ifk/ifk_hbechain_cli.c ifk/ifk_hbechain.c ifk/ifk_offchk.c ifk/ifk_tpconv.c ifk/ifk_hbefilter.c mcspline_fit.c mcspline.c mcam_load.c part_tools.c
ifk_hbechain_cli: $(IFK_HBECHAIN_SRC) ifk/ifk_hbechain.h ifk/ifk_offchk.h ifk/ifk_tpconv.h mcspline_fit.h
	$(CC) $(CFLAGS) -Wno-unused-function -Wno-format-truncation -o $@ $(IFK_HBECHAIN_SRC) -lm
ifk_hbefilter_test: ifk/ifk_hbefilter.c
	$(CC) $(CFLAGS) -DHBE_FILTER_TEST -o $@ ifk/ifk_hbefilter.c -lm
# interfacial11 Shell2D side path of Silhouette's test-point classifier (interfacial.md 11.8): self-test
ifk_s2_test: ifk/ifk_s2_test.c ifk/ifk_sil_shell2d.c ifk/ifk_sil_shell2d.h ifk/ifk_planecut.h
	$(CC) $(CFLAGS) -o $@ ifk/ifk_s2_test.c ifk/ifk_sil_shell2d.c -lm
# CSpinBdryArcFitter (mcmachinedef.dll, Turn Profile arc fitter, interfacial.md 14): tools/tests/ifk_arcfit_test.sh, ifk_arcfit_oracle.sh
ifk_arcfit_test: ifk/ifk_arcfit_test.c ifk/ifk_arcfit.c ifk/ifk_arcfit.h
	$(CC) $(CFLAGS) -ffp-contract=off -o $@ ifk/ifk_arcfit_test.c ifk/ifk_arcfit.c -lm

# the STL Turn Profile / Silhouette Boundary feed the kernels (wr_stl -> surf_to_stl -> make_surf_polygons, interfacial.md 12):
# ifk/ifk_surfpoly.c on mcpartline_mesh.c; tools/tests/ifk_surfpoly_test.sh (self-test), ifk_surfpoly_oracle.sh (against mccore.dll)
IFK_STL_SRC = ifk/ifk_stl_port.c ifk/ifk_surfpoly.c mcpartline.c mcpartline_mesh.c mcsurf_nrm.c mcsurf.c mcam_load.c mcspline.c mcspline_fit.c mcspline_simplify.c
ifk_stl_port: $(IFK_STL_SRC) ifk/ifk_surfpoly.h mcpartline.h mcpartline_trim.inc mcpartline_trim2.inc mcpartline_trim3.inc
	$(CC) $(CFLAGS) -Wno-unused-function -Wno-format-truncation -o $@ $(IFK_STL_SRC) -lm

# the classic finish engine's machine model (make_surf_model, formats/surf_classic3.md 12.9): tools/tests/msm_cmp.py against tools/peloader --msm
MSM_SRC = msm_port.c cam3d_mmodel.c mcpartline.c mcpartline_mesh.c mcsurf_nrm.c mcsurf.c mcam_load.c mcspline.c mcspline_fit.c mcspline_simplify.c
msm_port: $(MSM_SRC) mcpartline.h cam3d_mmodel.h mcpartline_trim.inc mcpartline_trim2.inc mcpartline_trim3.inc
	$(CC) $(CFLAGS) -Wno-unused-function -Wno-format-truncation -o $@ $(MSM_SRC) -lm

# the Constant Scallop scanner (FUN_1801ea7e0, formats/surf_classic3.md 12.12): tools/tests/scn_cmp.py against tools/peloader --lmm SCN
CS_SRC = cs_port.c cam3d_cscallop.c cam3d_cscallop_lines.c cam3d_cscallop_edge.c cam3d_cscallop_sect.c cam3d_cscallop_merge.c cam3d_cscallop_cfb.c cam3d_cscallop_wall.c nest/nest_rect.c cam3d_mmodel.c mcpartline.c mcpartline_mesh.c mcsurf_nrm.c mcsurf.c mcam_load.c mcspline.c mcspline_fit.c mcspline_simplify.c
cs_port: $(CS_SRC) mcpartline.h cam3d_mmodel.h cam3d_cscallop.h mcpartline_trim.inc mcpartline_trim2.inc mcpartline_trim3.inc
	$(CC) $(CFLAGS) -ffp-contract=off -Wno-unused-function -Wno-format-truncation -o $@ $(CS_SRC) -lm

# FUN_1801cfb60's per-line stages (formats/surf_classic3.md 12.19): tools/tests/cfbu_cmp.sh against tools/peloader --lmm CFBU
CFB_SRC = cfb_port.c cam3d_cscallop_zone.c cam3d_cscallop_zbuf.c cam3d_cscallop_zcore.c cam3d_cscallop_zwalk.c cam3d_cscallop_cfb.c cam3d_cscallop_fd80.c cam3d_cscallop_rgn.c cam3d_cscallop_wall.c cam3d_cscallop_emesh.c cam3d_cscallop_seg.c cam3d_cscallop_d15.c cam3d_cscallop_9a20.c cam3d_cscallop_6e60.c cam3d_cscallop_lines.c cam3d_cscallop_merge.c cam3d_cscallop.c cam3d_cscallop_edge.c cam3d_cscallop_sect.c nest/nest_rect.c
cfb_port: $(CFB_SRC) cam3d_cscallop.h
	$(CC) $(CFLAGS) -ffp-contract=off -Wno-unused-function -o $@ $(CFB_SRC) -lm

nc_dev: nc_dev.c
	$(CC) $(CFLAGS) -o $@ nc_dev.c -lm

xt2obj: xt2obj.c xt_mesh.c xt_mesh.h mcam_load.c mcam_load.h part_tools.c part_tools.h
	$(CC) $(CFLAGS) -Wno-format-truncation -o $@ xt2obj.c xt_mesh.c mcam_load.c mcspline.c part_tools.c -lm

clean:
	rm -f mcamview xt2obj cam_cli tool_cli nc_dev ifk_cli ifk_stl_port ifk_sil_test ifk_planecut_test ifk_s2_test ifk_hbefilter_test ifk_silrun_test ifk_hbechain_cli *.o; rm -rf core_obj

# Windows (MinGW-w64 cross compile from Linux, or MSYS2 on Windows): cam_cli.exe, plus the core objects of the Qt app as a
# compile check (the Windows build of the Qt app itself - viewer-qt CMake with MinGW / MSVC - is still open: phase 9 of
# formats/qt_port_plan.md). WINDEPS = a folder holding glew-2.2.0 (source) and sqlite-amalgamation-* (source).
#   make windows WINDEPS=/path/to/windeps        -> win/cam_cli.exe, win/*.o
WINCC ?= x86_64-w64-mingw32-gcc
WINCXX ?= x86_64-w64-mingw32-g++
WSQL = $(wildcard $(WINDEPS)/sqlite-amalgamation-*)
WINC = -I$(WINDEPS)/glew-2.2.0/include -I$(WSQL) -DGLEW_STATIC
WSRC = $(CORE_C)
windows:
	@test -n "$(WINDEPS)" || { echo "set WINDEPS (see the comment above)"; exit 1; }
	mkdir -p win
	$(WINCC) -O2 -c $(WSQL)/sqlite3.c -o win/sqlite3.o
	$(WINCXX) -O2 -c offset.cpp -o win/offset.o
	$(WINCXX) -O2 -c vendor/clipper6/clipper.cpp -o win/clipper6.o
	for f in cam_cli $(WSRC:.c=); do $(WINCC) -O2 -Wno-format-truncation $(WINC) -c $$f.c -o win/$$f.o || exit 1; done
	for f in $(CORE_UI); do $(WINCC) -O2 -Wno-format-truncation $(WINC) -I. -c core_ui/$$f.c -o win/$$f.o || exit 1; done
	$(WINCXX) -o win/cam_cli.exe win/cam_cli.o $(filter-out win/rpx.o win/scene.o win/geo2d.o win/solid25.o,$(addprefix win/,$(WSRC:.c=.o))) \
	    win/offset.o win/clipper6.o win/sqlite3.o -static-libgcc -static-libstdc++

.PHONY: all core clean windows
