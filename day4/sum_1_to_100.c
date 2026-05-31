#include <stdio.h>

int main()
{
    int sum = 0;

    // 从 1 加到 100
    for (int i = 1; i <= 100; i++)
    {
        sum += i;
    }

    printf("从 1 加到 100 的结果是：%d\n", sum);

    return 0;
}
