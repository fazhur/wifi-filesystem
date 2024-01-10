//
// Created by Богдан Пелех on 08.01.2024.
//

#include "SyncSemaphore.h"
#include <iostream>

SyncSemaphore::SyncSemaphore(const char* name, int initialValue) : sem_name(name) {
    semaphore = sem_open(name, O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, initialValue);
    if (semaphore == SEM_FAILED) {
        if (errno == EEXIST) {
            semaphore = sem_open(name, 0);
            if (semaphore == SEM_FAILED) {
                std::cerr << "sem_open(3) failed to open existing semaphore" << std::endl;
                exit(EXIT_FAILURE);
            }
        } else {
            std::cerr << "sem_open(3) error" << std::endl;
            exit(EXIT_FAILURE);
        }
    }
}


SyncSemaphore::~SyncSemaphore() {
    if (sem_close(semaphore) < 0) {
        std::cout << "sem_close(3) failed" << std::endl;
        if (sem_unlink(sem_name) < 0) {
            std::cout << "sem_unlink(3) failed" << std::endl;
        }
    }

}

void SyncSemaphore::wait() {
    if (sem_wait(semaphore) < 0) {
        std::cout << "sem_wait(3) failed" << std::endl;
    }
}

void SyncSemaphore::signal() {
    if (sem_post(semaphore) < 0) {
        std::cout << "sem_post(3) error" << std::endl;
    }
}

