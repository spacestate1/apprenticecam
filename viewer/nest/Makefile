# viewer/nest - NestLib engine ports + their oracle test drivers.
#   nest_rect_test: Rectangular engine (nest_rect.c)          tools/tests/nest_rect_oracle.sh
#   nest_ts_test:   TrueShape engine (nest_ts.c + ts/)         tools/tests/nest_ts_oracle.sh
#   nest_ts_job_test: Geometry Nesting's TrueShape back-end (nest_ts_job.c: NESTING.DLL's N* sequence on a NestJob
#                   text file, nest_job_io.c)                 tools/tests/nest_ts_job_oracle.sh
# -ffp-contract=off: no FMA contraction, keep MSVC x64 SSE2 double rounding (bit-exactness vs n64dll.dll).
CC ?= gcc
CFLAGS ?= -O2 -g -Wall -Wextra
CFLAGS += -ffp-contract=off -fno-fast-math
OBJS = nest_rect.o nest_rect_io.o
# ts/build/ts_gen.c is generated (ts/gen/gen.py) from Ghidra's decompile: compiled permissively, without warnings
TSGENFLAGS = -O1 -g -std=gnu17 -fpermissive -w -fwrapv -fno-strict-aliasing -ffp-contract=off -fno-fast-math
TSOBJS = nest_ts.o ts/ts_rt.o ts/ts_crt.o ts/ts_printf.o ts/ts_libm.o ts/build/ts_gen.o ts/build/ts_data.o

all: nest_rect_test nest_ts_test nest_ts_job_test

nest_rect_test: $(OBJS) nest_rect_main.o
	$(CC) $(CFLAGS) -o $@ $^ -lm

nest_ts_test: $(TSOBJS) nest_ts_main.o nest_rect.o
	$(CC) $(CFLAGS) -o $@ $^ -lm

nest_ts_job_test: $(TSOBJS) nest_ts_job.o nest_job_io.o nest_rect_job.o nest_ts_job_main.o nest_rect.o
	$(CC) $(CFLAGS) -o $@ $^ -lm

nest_ts_job.o: nest_ts_job.c nest_job.h nest_ts.h ts/build/ts_gen.h
	$(CC) $(CFLAGS) -std=gnu17 -fpermissive -w -c -o $@ $<

nest_ts.o: nest_ts.c nest_ts.h ts/build/ts_gen.h
	$(CC) $(CFLAGS) -std=gnu17 -fpermissive -w -c -o $@ $<

%.o: %.c nest_rect.h
	$(CC) $(CFLAGS) -c -o $@ $<

# objects whose pattern rule above does not list the job header (a stale one wrote another NestOutInst layout)
nest_job_io.o nest_rect_job.o nest_ts_job_main.o: nest_job.h nest_job_io.h

ts/%.o: ts/%.c ts/ts_rt.h ts/ts_crt.h
	$(CC) $(CFLAGS) -Wno-unused-parameter -c -o $@ $<

ts/build/%.o: ts/build/%.c ts/build/ts_gen.h ts/ghidra.h ts/ts_types.h ts/ts_rt.h ts/ts_crt.h
	$(CC) $(TSGENFLAGS) -c -o $@ $<

clean:
	rm -f *.o ts/*.o ts/build/*.o nest_rect_test nest_ts_test nest_ts_job_test

.PHONY: all clean
