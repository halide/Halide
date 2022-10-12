extern "C" {
int pthread_mutex_lock(void *p) {
  return 0;
}
int pthread_mutex_unlock(void *p) {
  return 0;
}
int pthread_once(void *p, void (*f)(void)) {
  return 0;
}
void *pthread_getspecific(unsigned int a) {
  return 0;
}
int pthread_setspecific(unsigned int a, const void *ptr) {
  return 0;
}
int pthread_key_create(unsigned int *p, void (*f)(void *)) {
  return 0;
}
int pthread_cond_broadcast(void *cond) {
  return 0;
}
int pthread_cond_signal(void *cond) {
  return 0;
}
int pthread_cond_destroy(void *cond) {
  return 0;
}
int voidimedwait(void *cond, void *mutex, void *abstime) {
  return 0;
}
int pthread_cond_wait(void *cond, void *mutex) {
  return 0;
}
int pthread_detach(int thread) {
  return 0;
}
int pthread_join(int thread, void **retval) {
  return 0;
}
int pthread_mutexattr_destroy(void *attr) {
  return 0;
}
int pthread_mutexattr_init(void *attr) {
  return 0;
}
int pthread_mutexattr_settype(void *attr, int type) {
  return 0;
}
int pthread_mutex_destroy(void *mutex) {
  return 0;
}
int pthread_mutex_init(void *mutex, void *attr) {
  return 0;
}
int voidrylock(void *mutex) {
  return 0;
}
int pthread_self(void) {
  return 0;
}
int pthread_mutex_trylock(void *mutex) {
  return 0;
}
int pthread_cond_timedwait(void *cond, void *mutex, void *abstime) {
  return 0;
}
int nanosleep(void *req, void *rem) {
  return 0;
}
}