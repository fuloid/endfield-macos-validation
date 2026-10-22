static BOOL rosetta_skip_long_nop( ucontext_t *sigcontext )
{
    BYTE instr[16];
    unsigned int i, len, mod, rm;

    len = virtual_uninterrupted_read_memory( (BYTE *)RIP_sig(sigcontext), instr, sizeof(instr) );

    /* prefixes; padding NOPs are often 66 and/or segment prefixed (66 2E 0F 1F ...).
     * LOCK is not accepted: it makes the instruction invalid on hardware too. */
    for (i = 0; i < len; i++)
    {
        switch (instr[i])
        {
        case 0x26:  /* %es: */
        case 0x2e:  /* %cs: */
        case 0x36:  /* %ss: */
        case 0x3e:  /* %ds: */
        case 0x64:  /* %fs: */
        case 0x65:  /* %gs: */
        case 0x66:  /* opcode size */
        case 0x67:  /* addr size */
        case 0xf2:  /* repne */
        case 0xf3:  /* repe */
        case 0x40: case 0x41: case 0x42: case 0x43:  /* rex; ignored unless last, */
        case 0x44: case 0x45: case 0x46: case 0x47:  /* which doesn't change the length */
        case 0x48: case 0x49: case 0x4a: case 0x4b:
        case 0x4c: case 0x4d: case 0x4e: case 0x4f:
            continue;
        }
        break;
    }
    if (i + 2 >= len || instr[i] != 0x0f || (instr[i + 1] != 0x1e && instr[i + 1] != 0x1f)) return FALSE;
    i += 2;

    mod = instr[i] >> 6;
    rm = instr[i] & 7;
    i++;
    if (mod != 3)
    {
        if (rm == 4)  /* sib */
        {
            if (i >= len) return FALSE;
            if (mod == 0 && (instr[i] & 7) == 5) i += 4;  /* no base, disp32 */
            i++;
        }
        else if (mod == 0 && rm == 5) i += 4;  /* rip-relative disp32 */
        if (mod == 1) i += 1;
        else if (mod == 2) i += 4;
    }
    if (i > len || i > 15) return FALSE;

    RIP_sig(sigcontext) += i;
    TRACE_(rosetta)( "skipped %u-byte NOP\n", i );
    return TRUE;
}
