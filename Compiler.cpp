#include "Compiler.h"


void panic(const char *fmt, ...) {
    char message[1024];
    va_list arglist;
    va_start(arglist, fmt);
    vsnprintf(message, 1024, fmt, arglist);
    va_end(arglist);
    printf(message);
    exit(-1);
}

void assert(bool cond, const char *fmt, ...) {
    if (cond) return;
    char message[1024];
    va_list arglist;
    va_start(arglist, fmt);
    vsnprintf(message, 1024, fmt, arglist);
    va_end(arglist);
    printf(message);
    exit(-1);
}


map<float, IRNode *> IRNode::floatInstances;
map<int, IRNode *> IRNode::intInstances;
map<OpCode, IRNode *> IRNode::varInstances;
vector<IRNode *> IRNode::allNodes;

