/* FUN_18005e640 L46650 (setter of params+0x410 = DAT_1802b4750, values 0 / 1).  The decompile shows the throw as
 * the end of the function; the DLL catches it in the same function: catch funclet Catch@18025acf0 L284694 copies the
 * nestErrorPrm object and calls FUN_180053920 with it, then continues at 0x18005e70b = the epilogue (asm
 * 0x18005e6c0..0x18005e724).  So an invalid value reports error 0x40f (severity 1, kind 2, argument = the value) and
 * leaves the parameter unchanged. */
void FUN_18005e640(int param_1)
{
  undefined1 local_1028[0x400];
  if ((param_1 != 0) && (param_1 != 1)) {
    FUN_1800537a0((undefined4 *)local_1028, 0x40f, 1, 2, param_1);
    FUN_180053920(0, (undefined4 *)local_1028);
    return;
  }
  TS_G(int, 0x1802b4750) = param_1;   /* _DAT_1802b4750 */
}
