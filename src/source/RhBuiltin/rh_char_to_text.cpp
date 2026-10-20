struct SysVView { char* ptr; long len; };
extern "C" void* rh_arena_alloc(unsigned long long size);
extern "C" struct SysVView rh_char_to_text(long long ch) {
    char* buf = (char*)rh_arena_alloc(1);
    buf[0] = (char)(ch & 0xFF);
    SysVView r = { buf, 1 };
    return r;
}
