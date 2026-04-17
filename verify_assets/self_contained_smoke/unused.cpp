static int unused_data = 99;

extern "C" int unused_toggle(int value)
{
    return value ^ unused_data;
}
