/* nest_job_io.h - NestJob / NestOut as exact text (nest_job_io.c) for the engine oracles */
#ifndef NEST_JOB_IO_H
#define NEST_JOB_IO_H
#include "nest_job.h"
int nest_job_write(const char *path, const NestJob *j);
int nest_job_read(const char *path, NestJob *j);
void nest_job_free(NestJob *j);
int nest_out_write(const char *path, int ok, const NestOut *o, const char *err);
#endif
