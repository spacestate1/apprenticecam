/* FUN_1800f8490 L112084: copies a profile list (param_1) into param_2: a new pointer array of +0xfc entries at +0x150,
 * +0xfc / +0x138 / +0x148 copied, then per entry a new 0x158 record with a new array of 200-byte items at +0x138 and
 * the entry's +0xfc / +0x148 / +0x150 / +0x108.  The decompile loses the allocations' results (extraout_RAX: it takes
 * FUN_1800a2630 as returning its argument and stores an uninitialised value at +0x138), so this is transliterated from
 * the asm 0x1800f8490..0x1800f8665 (frame slots in the comments). */
undefined8 FUN_1800f8490(longlong param_1, longlong param_2, undefined8 param_3, undefined8 param_4)
{
  longlong *src, *dst, ent, rec;           /* [rsp+0x40] [rsp+0x28] [rsp+0x30] [rsp+0x20] */
  int i;                                   /* [rsp+0x38] */
  (void)param_3; (void)param_4;
  *(void **)(param_2 + 0x150) = FUN_1800a2510((ulonglong)((longlong)*(int *)(param_1 + 0xfc) << 3), 0, 0, 0);   /* 1800f84b1 */
  FUN_1800a2630(*(undefined8 *)(param_2 + 0x150), 1);                                                         /* 1800f84d2 */
  *(int *)(param_2 + 0xfc) = *(int *)(param_1 + 0xfc);                                                        /* 1800f84e7 */
  memcpy((char *)param_2 + 0x138, (char *)param_1 + 0x138, 8);                                                /* movsd 1800f84f7 */
  memcpy((char *)param_2 + 0x148, (char *)param_1 + 0x148, 8);                                                /* movsd 1800f8511 */
  src = *(longlong **)(param_1 + 0x150);                                                                      /* 1800f852d */
  dst = *(longlong **)(param_2 + 0x150);                                                                      /* 1800f853e */
  for (i = 0; i < *(int *)(param_1 + 0xfc); i++) {                                                            /* 1800f8543..8566 */
    ent = *src;                                                                                               /* 1800f8574 */
    rec = (longlong)FUN_1800a2510(0x158, 0, 0, 0);                                                            /* 1800f857e */
    FUN_1800a2630((undefined8)rec, 1);                                                                        /* 1800f8591 */
    *(void **)(rec + 0x138) = FUN_1800a2510((ulonglong)((longlong)*(int *)(ent + 0xfc) * 200), 0, 0, 0);      /* 1800f85ac..85b6 */
    FUN_1800a2630(*(undefined8 *)(rec + 0x138), 1);                                                           /* 1800f85cd */
    *(int *)(rec + 0xfc) = *(int *)(ent + 0xfc);                                                              /* 1800f85e2 */
    memcpy((char *)rec + 0x148, (char *)ent + 0x148, 8);                                                      /* movsd 1800f85f2 */
    *(int *)(rec + 0x150) = *(int *)(ent + 0x150);                                                            /* 1800f8612 */
    *(int *)(rec + 0x108) = *(int *)(ent + 0x108);                                                            /* 1800f8628 */
    *dst = rec;                                                                                               /* 1800f8638 */
    src++; dst++;                                                                                             /* 1800f8640..8652 */
  }
  return 1;
}
