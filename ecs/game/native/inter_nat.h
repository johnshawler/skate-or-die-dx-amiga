/* INTER's timed wait ($7755-$777A: about 65,000 turns of a loop that polls the keyboard and the button, 1.4 s on the IIGS): the platform waits that long, or until a
   key or the button comes, and the code goes on at $777B. */
void rt_inter_wait(void);
static int nat_inter_wait(void)
{
    rt_inter_wait();
    return 1;
}
