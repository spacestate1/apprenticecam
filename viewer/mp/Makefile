# viewer/mp - MP post engine (port of mcpost.dll); see ../../formats/mp_engine.md
# Each module may append its own targets below. Objects go to obj/. Files outside viewer/mp/ are only read.
CC      = gcc
CFLAGS  = -O2 -g -Wall -std=gnu11 -I. -I..
LDLIBS  = -lm
OBJ     = obj

all:
.PHONY: all clean
clean:
	rm -rf $(OBJ)/*.o $(BINS)

$(OBJ)/%.o: %.c
	@mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c $< -o $@

# viewer/*.c compiled read-only into obj/ (e.g. obj/v_mcam_load.o)
$(OBJ)/v_%.o: ../%.c
	@mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -c $< -o $@

# ---- module targets (append below; add each binary to BINS and to all:) ----

# ---- mp_fmt: number formatting (fs2/fs) + self-test / oracle CLI ----
BINS += fmt_test
all: fmt_test
fmt_test: $(OBJ)/mp_fmt.o $(OBJ)/fmt_test.o
	$(CC) $(CFLAGS) -o $@ $^ -lm

# ---- interpreter (mp_parse / mp_run / mp_eval / mp_out / mp_nciloop) + CLI: interpreter agent ----
MP_FN_OBJ  = $(if $(wildcard mp_eval_fn.c),$(OBJ)/mp_eval_fn.o,$(OBJ)/mp_eval_fnstub.o)
MP_INT_OBJ = $(OBJ)/mp_parse.o $(OBJ)/mp_parse_tables.o $(OBJ)/mp_run.o $(OBJ)/mp_eval.o $(OBJ)/mp_out.o $(OBJ)/mp_fmt.o $(MP_FN_OBJ)
MP_CLI_EXTRA = $(OBJ)/mp_nciloop.o $(OBJ)/mp_run_prm.o $(OBJ)/mp_prm.o $(OBJ)/nci_ascii.o $(OBJ)/v_mcam_load.o $(OBJ)/v_mcspline.o $(OBJ)/v_part_tools.o
$(OBJ)/mp_cli.o: CFLAGS += -DMP_HAVE_NCI_ASCII -DMP_HAVE_NCILOOP
mp_cli: $(OBJ)/mp_cli.o $(MP_INT_OBJ) $(MP_CLI_EXTRA)
	$(CC) $(CFLAGS) -o $@ $^ $(PRM_LDEXTRA) $(LDLIBS)
mp_parsecheck: $(OBJ)/mp_parsecheck.o $(OBJ)/mp_parse.o $(OBJ)/mp_parse_tables.o $(OBJ)/mp_fmt.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)
BINS += mp_cli mp_parsecheck
all: mp_cli

# ---- mp_prm (MD / CD / group / op parameters) + prm_dump test CLI ----
PRM_DEPS = $(OBJ)/mp_prm.o $(OBJ)/v_mcam_load.o $(OBJ)/v_mcspline.o $(OBJ)/v_part_tools.o
BINS += prm_dump
all: prm_dump
prm_dump: $(OBJ)/prm_dump.o $(PRM_DEPS)
	$(CC) $(CFLAGS) -o $@ $^ $(PRM_LDEXTRA) $(LDLIBS)

# ---- nci_ascii (binary NCI -> ASCII .nci, port of mccore bnci_to_nci) + nci_dump CLI ----
BINS += nci_dump
all: nci_dump
nci_dump: $(OBJ)/nci_dump.o $(OBJ)/nci_ascii.o $(OBJ)/v_mcam_load.o $(OBJ)/v_mcspline.o $(OBJ)/v_part_tools.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)
# interpreter objects depend on the private header (interpreter agent)
$(OBJ)/mp_parse.o $(OBJ)/mp_parse_tables.o $(OBJ)/mp_run.o $(OBJ)/mp_run_prm.o $(OBJ)/mp_eval.o $(OBJ)/mp_eval_fn.o $(OBJ)/mp_eval_fnstub.o $(OBJ)/mp_out.o $(OBJ)/mp_cli.o $(OBJ)/mp_parsecheck.o $(OBJ)/mp_nciloop.o: mp_int.h mp.h mp_fmt.h mp_parse_tables.h
$(OBJ)/mp_nciloop.o: mp_nciloop_tt.c
$(OBJ)/mp_nciloop.o: mp_nciloop_sub.c

# ---- mp_post: the app's chain (core_ui post_ops; built into the Qt app by viewer-qt/CMakeLists.txt MP_C) - compile check ----
all: $(OBJ)/mp_post.o
$(OBJ)/mp_post.o: mp_post.h nci_ascii.h mp_int.h mp.h
# every object sees McamFile / Op through mp.h / nci_ascii.h -> ../mcam_load.h: rebuilt when it changes (batch 11 added fields;
# without this the stale objects of nci_dump / mp_cli read a shifted struct and crashed)
$(patsubst %.c,$(OBJ)/%.o,$(wildcard *.c)): ../mcam_load.h ../part_tools.h
