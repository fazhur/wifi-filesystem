//
// Created by Богдан Пелех on 08.01.2024.
//

#ifndef EXAM_SYNCSEMAPHORE_H
#define EXAM_SYNCSEMAPHORE_H

#include <semaphore.h>

#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>

class SyncSemaphore {
public:
    SyncSemaphore(const char* name, int initialValue);
    ~SyncSemaphore();

    void wait();
    void signal();

private:
    sem_t *semaphore;
    const char* sem_name;
};


#endif //EXAM_SYNCSEMAPHORE_H
