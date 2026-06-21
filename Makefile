default:
	gcc -ggdb -Wall -Wextra -Wpedantic main.c switch.s runtime.c netpoller.c queue.c sync.c io.c scheduler.c fiber.c -o a
