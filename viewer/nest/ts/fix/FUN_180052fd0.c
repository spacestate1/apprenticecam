/* FUN_180052fd0 L42126: raises NestLib error PARAM_1 (severity PARAM_2, or the message table's when it has one; kind
 * PARAM_3) - variadic like FUN_1800537a0: the decompile's &local_res20 (the home slot of the 4th argument) is the va_list
 * of the arguments from the 4th on (MS x64 varargs), used by both vsprintf calls (FUN_1802293e4 = vsprintf); the
 * generated transliteration passed that address as a SysV va_list and crashed on the first %s (error 705 "Sheet : %s.
 * Size too small.", y026 / y029 of nest_ts_job_oracle.sh -s 13). */
uint64_t FUN_180052fd0(int param_1, int param_2, uint param_3, ...)
{
  TS_ENTER(0x180052fd0ULL);
  undefined1 local_838[1008];
  undefined1 local_438[1008];
  undefined1 local_38[32];
  va_list ap;
  FUN_1800536a0(local_38, param_1);
  char *fmt1 = *(char **)(local_38 + 16), *fmt2 = *(char **)(local_38 + 8);   /* local_28 / local_30 */
  int sev = param_2;
  if (*(int *)(local_38 + 24) != 0) sev = *(int *)(local_38 + 24);           /* local_20 */
  va_start(ap, param_3); ts_vsprintf((char *)local_838, fmt1, ap); va_end(ap);
  va_start(ap, param_3); ts_vsprintf((char *)local_438, fmt2, ap); va_end(ap);
  return (uint64_t)FUN_1800538d0(param_1, sev, param_3, (undefined8)local_438);
}
