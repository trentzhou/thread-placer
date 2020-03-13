test_thread_placer: thread_placer.c main.c
	gcc -o $@  $+ -lpthread

clean:
	rm -f test_thread_placer
