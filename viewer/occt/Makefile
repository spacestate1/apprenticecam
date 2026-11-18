# XT -> OCCT translator + accuracy check (formats/occt_accuracy.md). Standalone: does not touch the main build.
# make            -> $(OUT)/xt_occt_check
# make check      -> extract the user's solids and run the check ($(OUT)/accuracy.tsv)
OUT ?= $(HOME)/mastercam-re/work/occt
OCC_INC ?= /usr/include/opencascade
CXX ?= g++
CXXFLAGS ?= -O2 -g -std=c++17 -Wall -Wno-unused-parameter
OCC_LIBS = -lTKBO -lTKBool -lTKMesh -lTKShHealing -lTKTopAlgo -lTKGeomAlgo -lTKBRep -lTKGeomBase -lTKG3d -lTKG2d -lTKMath -lTKernel
PARTS ?= <user-parts>/*.mcx-9
SCHEMA ?= $(HOME)/mastercam-re/files/sch_26105.s_t

all: $(OUT)/xt_occt_check

$(OUT)/xt_mesh.o: ../xt_mesh.c ../xt_mesh.h
	@mkdir -p $(OUT)
	$(CC) -O2 -g -std=gnu11 -I.. -c ../xt_mesh.c -o $@

$(OUT)/xt_occt.o: xt_occt.cpp xt_occt.h
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) -I$(OCC_INC) -c xt_occt.cpp -o $@

$(OUT)/xt_occt_check: xt_occt_check.cpp $(OUT)/xt_occt.o $(OUT)/xt_mesh.o
	$(CXX) $(CXXFLAGS) -I$(OCC_INC) -I.. xt_occt_check.cpp $(OUT)/xt_occt.o $(OUT)/xt_mesh.o $(OCC_LIBS) -lm -o $@

check: $(OUT)/xt_occt_check
	python3 xt_extract.py $(OUT)/xb $(PARTS)
	( ulimit -v 4000000; $(OUT)/xt_occt_check $(SCHEMA) $(OUT)/xb/manifest.tsv ) > $(OUT)/accuracy.tsv

# mutation tests (ours): the check must flag a deliberately wrong translator
MUTANTS = CIRCLE_SENSE SURFACE_SENSE RADIUS
mutate: $(OUT)/xt_mesh.o
	for M in $(MUTANTS); do \
	  $(CXX) $(CXXFLAGS) -DXTOCC_MUTATE_$$M -I$(OCC_INC) -I.. xt_occt_check.cpp xt_occt.cpp $(OUT)/xt_mesh.o $(OCC_LIBS) -lm -o $(OUT)/mutant_$$M && \
	  ( ulimit -v 4000000; $(OUT)/mutant_$$M $(SCHEMA) $(OUT)/xb/manifest.tsv ) > $(OUT)/mutant_$$M.tsv; done
	$(CXX) $(CXXFLAGS) -DXTOCC_MUTATE_RADIUS -DXTOCC_MUTATE_LOOSE -I$(OCC_INC) -I.. xt_occt_check.cpp xt_occt.cpp $(OUT)/xt_mesh.o $(OCC_LIBS) -lm -o $(OUT)/mutant_RADIUS_LOOSE
	( ulimit -v 4000000; $(OUT)/mutant_RADIUS_LOOSE $(SCHEMA) $(OUT)/xb/manifest.tsv ) > $(OUT)/mutant_RADIUS_LOOSE.tsv

.PHONY: all check mutate

# OCCT -> XT writer + its oracle (formats/occt_xt.md)
$(OUT)/occt_xt.o: occt_xt.cpp occt_xt.h xt_refgeom.h xt_occt.h
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) -I$(OCC_INC) -c occt_xt.cpp -o $@

$(OUT)/occt_xt_check: occt_xt_check.cpp $(OUT)/occt_xt.o $(OUT)/xt_occt.o $(OUT)/xt_mesh.o
	$(CXX) $(CXXFLAGS) -I$(OCC_INC) -I.. occt_xt_check.cpp $(OUT)/occt_xt.o $(OUT)/xt_occt.o $(OUT)/xt_mesh.o $(OCC_LIBS) -lTKPrim -lTKFillet -lTKOffset -lm -o $@

xtcheck: $(OUT)/occt_xt_check
	( ulimit -v 4000000; $(OUT)/occt_xt_check rt $(SCHEMA) $(OUT)/xb/manifest.tsv $(OUT)/xtw/rt1 1 ) > $(OUT)/xtw/rt_scale1.tsv
	( ulimit -v 4000000; $(OUT)/occt_xt_check rt $(SCHEMA) $(OUT)/xb/manifest.tsv $(OUT)/xtw/rt1000 1000 ) > $(OUT)/xtw/rt_scale1000.tsv
	( ulimit -v 4000000; $(OUT)/occt_xt_check synth $(SCHEMA) $(OUT)/xtw/synth ) > $(OUT)/xtw/synth.tsv
	python3 xt_canon.py $(SCHEMA) $(OUT)/xb/manifest.tsv $(OUT)/xtw/rt1 > $(OUT)/xtw/canon_scale1.txt
	python3 xt_canon.py $(SCHEMA) $(OUT)/xb/manifest.tsv $(OUT)/xtw/rt1000 > $(OUT)/xtw/canon_scale1000.txt

.PHONY: xtcheck

# Extrude history replay in OCCT + the end-to-end proof (formats/occt_xt.md "Proof"); links the app's loader read-only
$(OUT)/sld_regen: sld_regen.cpp $(OUT)/occt_xt.o $(OUT)/xt_occt.o ../mcam_load.o ../part_tools.o
	$(CXX) $(CXXFLAGS) -I$(OCC_INC) -I.. sld_regen.cpp $(OUT)/occt_xt.o $(OUT)/xt_occt.o ../mcam_load.o ../part_tools.o $(OCC_LIBS) -lTKPrim -lm -o $@
