#include "hiredis/hiredis.h"
#include <stdio.h>
#include <stdlib.h>

redisContext *redis_connect;

/*
 * store key and value to redis
 * Input: key and value
 * Output: if success, return 1; otherwise return 0
 */
void store_to_redis(char key[], char value[]) { // bad: need to set the return value
	redisCommand(redis_connect, "SET %s %s", key, value);
}

/* 
 * build a redis connection and store key-value pair to it
 * Input: key-value pair
 * Output: whether store is successful
 */
int main() {

	redis_connect = redisConnect("localhost", 6379);
	if (redis_connect != NULL && redis_connect->err) {
		printf("connect err\n");
		exit(1);
	}

	printf("Please input key and value:\n");
	char key[100];
	char value[100];
	if (scanf("%s %s\n", key, value) == 2) {
		store_to_redis(key, value);
	}
	else {
		printf("Input Wrong!\n");
		exit(1);
	}
	printf("Store Succeed!\n");
	return 0;
}
