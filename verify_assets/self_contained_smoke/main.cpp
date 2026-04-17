extern "C" int helper_add(int value);
extern volatile int g_buffer[8];

int g_counter = 7;
const char kBanner[] = "ld_patcher json smoke";

int main()
{
    g_buffer[0] = helper_add(g_counter) + static_cast<int>(kBanner[0]);
    return g_buffer[0] & 0xff;
}
