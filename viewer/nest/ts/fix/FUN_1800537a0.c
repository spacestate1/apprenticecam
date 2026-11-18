/* FUN_1800537a0 L42224: builds a nestErrorPrm (code, severity, kind, message) - variadic: the decompile's
 * &stack0x00000028 is the va_list of the arguments after the 4 named ones (asm: lea of the caller's 5th slot), used
 * by both vsprintf calls (FUN_1802293e4 = vsprintf). */
undefined4 * FUN_1800537a0(undefined4 *param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, ...)
{
  undefined1 local_838[1008];
  undefined1 local_438[1008];
  undefined1 local_38[32];
  va_list ap;
  FUN_1800536a0(local_38, param_2);
  char *fmt1 = *(char **)(local_38 + 16), *fmt2 = *(char **)(local_38 + 8);   /* local_28 / local_30 */
  va_start(ap, param_4); ts_vsprintf((char *)local_838, fmt1, ap); va_end(ap);
  va_start(ap, param_4); ts_vsprintf((char *)local_438, fmt2, ap); va_end(ap);
  *param_1 = param_2;
  strcpy((char *)(param_1 + 3), (char *)local_438);
  param_1[1] = param_3;
  param_1[2] = param_4;
  return param_1;
}
