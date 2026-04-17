volatile int g_buffer[8];

extern "C" int helper_add(int value)
{
    return value + 3;
}
