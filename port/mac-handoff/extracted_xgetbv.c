static BOOL rosetta_emulate_xgetbv( ucontext_t *sigcontext )
{
    BYTE instr[3];
    unsigned int len = virtual_uninterrupted_read_memory( (BYTE *)RIP_sig(sigcontext), instr, sizeof(instr) );

    /* prefixed xgetbv is illegal, so no need to check for prefixes */
    if (len < 3 || instr[0] != 0x0f || instr[1] != 0x01 || instr[2] != 0xd0) return FALSE;
    if ((RCX_sig(sigcontext) & 0xffffffff) != 0) return FALSE;

    RDX_sig(sigcontext) = 0;
    /* Rosetta on macOS 15+ supports AVX/AVX2 */
    RAX_sig(sigcontext) = sequoia_or_later ? 0xe7 : 0x07;
    RIP_sig(sigcontext) += 3;
    TRACE_(seh)( "emulated xgetbv\n" );
    return TRUE;
}
