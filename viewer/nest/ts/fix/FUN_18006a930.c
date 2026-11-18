/* FUN_18006a930 L52814: orders the param_3 columns of a part raster for the column fit (middle column first, then
 * the multiples of n/4, n/8 ... from both ends, then the rest in order) and writes the order back into param_1.
 * The decompile is wrong here (it folds n/2 to 0: "*(lVar3 + -1) = 1", "param_1 + -2", and the loop condition
 * "local_18 < 1"), so this is transliterated from the asm 0x18006a930..0x18006acc9 (frame slots in the comments). */
void FUN_18006a930(longlong param_1, undefined2 *param_2, ushort param_3)
{
  ushort *src = (ushort *)param_1;
  ushort i20 = 0;                         /* [rsp+0x20] next output slot */
  ushort i38;                             /* [rsp+0x38] */
  int h34, d50, j4c, k48, q30, a3c, b54;  /* [rsp+0x34] [rsp+0x50] [rsp+0x4c] [rsp+0x48] [rsp+0x30] [rsp+0x3c] [rsp+0x54] */
  char *buf;                              /* [rsp+0x28] "column taken" flags */
  if ((int)param_3 < 4) return;                                           /* 18006a98c */
  buf = (char *)FUN_1800a2510((ulonglong)param_3, 0, 0, 0);               /* 18006a9b0 */
  FUN_1800a2630((undefined8)buf, 1);                                      /* 18006a9c3 */
  for (i38 = 0; (int)i38 < (int)param_3; i38++) buf[i38] = 0;             /* 18006a9cf..9fb */
  h34 = (int)param_3 / 2;                                                 /* 18006a9fd */
  param_2[i20] = src[h34 - 1]; i20++; buf[h34 - 1] = 1;                   /* 18006aa0e..aa48 */
  d50 = 4;                                                                /* 18006aa4c */
  for (;;) {                                                              /* 18006aa54 */
    i38 = 1; j4c = 1;                                                     /* 18006aa5f..aa69 */
    if (d50 > (int)param_3 / 2) {                                         /* 18006aa7e: jle 18006ab5f */
      for (i38 = 0; (int)i38 < (int)param_3; i38++)                       /* 18006aa88..aaf9 */
        if (buf[i38] == 0) { param_2[i20] = src[i38]; i20++; buf[i38] = 1; }
      for (i38 = 0; (int)i38 < (int)param_3; i38++) src[i38] = param_2[i38]; /* 18006aafb..ab3f */
      FUN_1800a2560(buf);                                                 /* 18006ab46 */
      FUN_1800a2790((longlong)buf);                                       /* 18006ab50 */
      return;
    }
    h34 = (int)param_3 / d50;                                             /* 18006ab5f */
    q30 = (int)param_3 / h34;                                             /* 18006ab70 */
    k48 = ((q30 / 2) * 2 == q30) ? q30 - 1 : q30;                         /* 18006ab81..abbb */
    while (j4c <= k48) {                                                  /* 18006abbf */
      a3c = h34 * j4c;                                                    /* 18006abcd */
      if (buf[a3c - 1] == 0) { param_2[i20] = src[a3c - 1]; i20++; buf[a3c - 1] = 1; }
      j4c += 2;                                                           /* 18006ac2d */
      if (j4c > k48) break;                                               /* 18006ac3c */
      b54 = h34 * k48;                                                    /* 18006ac42 */
      if (buf[b54 - 1] == 0) { param_2[i20] = src[b54 - 1]; i20++; buf[b54 - 1] = 1; }
      k48 -= 2;                                                           /* 18006aca2 */
    }
    d50 <<= 1;                                                            /* 18006acb6 */
  }
}
