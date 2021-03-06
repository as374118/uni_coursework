#include "node.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "err.h"
#include "message.h"

void sendMessage(node_ptr node, message_ptr m) {
    writeMessage(node->mainWriteDescriptor, m);
}

node_ptr createEmptyNode() {
    node_ptr ret = (node_ptr)malloc(sizeof(node_t));
    ret->val = LINF;
    ret->type = -1;
    ret->inputDescriptors = createEmptyList();
    ret->outputDescriptors = createEmptyList();
    ret->operation = 0;
    ret->receivedVals = 0;
    ret->isProcessed = 0;
    allNodes[nodesCount++] = ret;
    return ret;
}

node_ptr createValueNode(long value) {
    node_ptr ret = createEmptyNode();
    ret->type = VALUE_NODE;
    ret->val = value;
    int temp = 0;
    createPipe(&temp, &ret->mainWriteDescriptor);
    addInt(&ret->inputDescriptors, temp);
    return ret;
}

node_ptr createBinaryOperationNode(char operation, node_ptr in1, node_ptr in2) {
    node_ptr ret = createEmptyNode();
    ret->type = BINARY_OPERATION_NODE;
    ret->operation = operation;
    ret->receivedVals = (message_ptr *)calloc(MAX_OPS, sizeof(message_ptr));
    ret->isProcessed = (int *)calloc(MAX_OPS, sizeof(int));
    tieNodes(in1, ret);
    tieNodes(in2, ret);
    return ret;
}

node_ptr createUnaryOperationNode(char operation, node_ptr in) {
    node_ptr ret = createEmptyNode();
    ret->type = UNARY_OPERATION_NODE;
    ret->operation = operation;
    tieNodes(in, ret);
    return ret;
}

node_ptr createVariableNode(int id) {
    node_ptr ret = createEmptyNode();
    ret->type = VARIABLE_NODE;
    ret->operation = 'x';
    ret->val = id;
    ret->isProcessed = (int *)calloc(MAX_OPS, sizeof(int));
    int temp = 0;
    createPipe(&temp, &ret->mainWriteDescriptor);
    addInt(&ret->inputDescriptors, temp);
    return ret;
}

node_ptr getOrCreateVariableNode(int id) {
    if (variables[id] == 0) {
        variables[id] = createVariableNode(id);
    }
    return variables[id];
}

void tieNodes(node_ptr sender,
              node_ptr receiver) { // creates a pipe between the two
    int read, write;
    createPipe(&read, &write);
    addInt(&sender->outputDescriptors, write);
    addInt(&receiver->inputDescriptors, read);
}

void dispatchInitialValues(int id, long *vals, int *isInCirciut) {
    for (int i = 0; i < MAX_VARS; i++) {
        if (isInCirciut[i]) {
            if (vals[i] != LINF) {
                message_ptr msg = createStartWithValMessage(id, vals[i]);
                sendMessage(getOrCreateVariableNode(i), msg);
                deleteMessage(msg);
            } else {
                message_ptr msg = createStartMessage(id);
                sendMessage(getOrCreateVariableNode(i), createStartMessage(id));
                deleteMessage(msg);
            }
        }
    }
}

void dispatchConsts(int init_id) {
    for (int i = 0; i < nodesCount; i++) {
        if (allNodes[i]->type == VALUE_NODE) {
            sendMessage(allNodes[i], createStartMessage(init_id));
        }
    }
}

void killAllProcesses() {
    for (int i = 0; i < nodesCount; i++) {
        if (allNodes[i]->type == VALUE_NODE ||
            allNodes[i]->type == VARIABLE_NODE) {
            sendMessage(allNodes[i], createExitMessage());
        }
    }
    for (int i = 0; i < nodesCount; i++) {
        wait(0);
    }
}

void valueNodeLoop(node_ptr node) {
    while (1) {
        message_ptr in = readFromAll(node->inputDescriptors);
        switch (in->type) {
        case EXIT_MESSAGE: {
            deleteMessage(in);
            message_ptr out = createExitMessage();
            writeToAll(node->outputDescriptors, out);
            deleteMessage(out);
            deleteNode(node);
            exit(0);
            break;
        }
        case START_MESSAGE: {
            message_ptr out = createResultMessage(in->init_id, node->val);
            writeToAll(node->outputDescriptors, out);
            deleteMessage(out);
            break;
        }
        default:
            syserr("unsupported message %d", in->type);
        }
        deleteMessage(in);
    }
}

void binaryOperationNodeLoop(node_ptr node) {
    while (1) {
        message_ptr in = readFromAll(node->inputDescriptors);
        printMessage(in);
        switch (in->type) {
        case EXIT_MESSAGE:
            deleteMessage(in);
            deleteNode(node);
            exit(0);
            break;
        case UNDEFINED_RESULT_MESSAGE: {
            if (node->isProcessed[in->init_id]) {
                break; // already processed this, ignoring
            }
            node->isProcessed[in->init_id] = 1;
            message_ptr out = createUndefinedResultMessage(in->init_id);
            writeToAll(node->outputDescriptors, out);
            deleteMessage(out);
            deleteMessage(node->receivedVals[in->init_id]);
            node->receivedVals[in->init_id] = 0;
            deleteMessage(in);
            break;
        }
        case RESULT_MESSAGE: {
            if (node->isProcessed[in->init_id] != 0) {
                break; // already processed this, ignoring
            }
            if (node->receivedVals[in->init_id] != 0) {
                long a = in->value;
                long b = node->receivedVals[in->init_id]->value;
                long res = 0;
                if (node->operation == '+') {
                    res = a + b;
                } else if (node->operation == '*') {
                    res = a * b;
                } else {
                    syserr("operator %c not supported", node->operation);
                }
                message_ptr out = createResultMessage(in->init_id, res);
                writeToAll(node->outputDescriptors, out);
                deleteMessage(out);
                node->isProcessed[in->init_id] = 1;
                deleteMessage(node->receivedVals[in->init_id]);
                node->receivedVals[in->init_id] = 0;
                deleteMessage(in);
            } else {
                node->receivedVals[in->init_id] = in;
            }
            break;
        }
        default:
            syserr("unsupported operation %d", in->type);
            deleteMessage(in);
            break;
        }
    }
}

void unaryOperationNodeLoop(node_ptr node) {
    while (1) {
        message_ptr in = readFromAll(node->inputDescriptors);
        switch (in->type) {
        case EXIT_MESSAGE: {
            deleteMessage(in);
            message_ptr out = createExitMessage();
            writeToAll(node->outputDescriptors, out);
            deleteMessage(out);
            deleteNode(node);
            exit(0);
            break;
        }
        case RESULT_MESSAGE: {
            message_ptr out = createResultMessage(in->init_id, -1 * in->value);
            writeToAll(node->outputDescriptors, out);
            deleteMessage(out);
            break;
        }
        case UNDEFINED_RESULT_MESSAGE: {
            message_ptr out = createUndefinedResultMessage(in->init_id);
            writeToAll(node->outputDescriptors, out);
            deleteMessage(out);
            break;
        }
        default:
            syserr("unsupported message %d", in->type);
        }
        deleteMessage(in);
    }
}

void variableNodeLoop(node_ptr node) {
    while (1) {
        message_ptr in = readFromAll(node->inputDescriptors);
        printMessage(in);
        switch (in->type) {
        case EXIT_MESSAGE: {
            deleteMessage(in);
            message_ptr out = createExitMessage();
            writeToAll(node->outputDescriptors, out);
            deleteMessage(out);
            deleteNode(node);
            exit(0);
            break;
        }
        case START_MESSAGE:
            if (node->isProcessed[in->init_id] != 0) {
                break; // this id was processed already, ignoring
            }
            if (getLen(node->inputDescriptors) == 1) { // only main input
                // we didn't get an initial value, so we should just send
                // undefined
                message_ptr out = createUndefinedResultMessage(in->init_id);
                writeToAll(node->outputDescriptors, out);
                deleteMessage(out);
                node->isProcessed[in->init_id] = 1;
            }
            break;
        case UNDEFINED_RESULT_MESSAGE: // fall through
        case RESULT_MESSAGE:           // fall through
        case START_WITH_VAL_MESSAGE: { // this message always comes before the
                                       // results from parent nodes
            if (node->isProcessed[in->init_id] != 0) {
                break; // this id was processed already, ignoring
            }
            message_ptr out = 0;
            if (in->type == UNDEFINED_RESULT_MESSAGE) {
                out = createUndefinedResultMessage(in->init_id);
            } else {
                out = createResultMessage(in->init_id, in->value);
            }
            writeToAll(node->outputDescriptors, out);
            deleteMessage(out);
            node->isProcessed[in->init_id] = 1;
            break;
        }
        default:
            syserr("var %d received unsupported message %d", node->val,
                   in->type);
            break;
        }
        deleteMessage(in);
    }
}

void nodeLoop(node_ptr node) {
    switch (node->type) {
    case VALUE_NODE:
        valueNodeLoop(node);
        break;
    case BINARY_OPERATION_NODE:
        binaryOperationNodeLoop(node);
        break;
    case UNARY_OPERATION_NODE:
        unaryOperationNodeLoop(node);
        break;
    case VARIABLE_NODE:
        variableNodeLoop(node);
        break;
    default:
        syserr("unknown node type %d", node->type);
        break;
    }
}

void startProcess(node_ptr node) {
    switch (fork()) {
    case -1:
        syserr("fork failed");
        break;
    case 0:
        if (close(node->mainWriteDescriptor) == -1) {
            syserr("close mainWriteDescriptor %d", node->mainWriteDescriptor);
        }
        nodeLoop(node);
        exit(0);
        break;
    default:
        break;
    }
}

void startProcessesForAllNodes() {
    for (int i = 0; i < nodesCount; i++) {
        startProcess(allNodes[i]);
    }
}

void deleteNode(node_ptr n) {
    if (n == 0) {
        return;
    }
    deleteListOfInts(n->inputDescriptors);
    deleteListOfInts(n->outputDescriptors);
    free(n->isProcessed);
    if (n->receivedVals != 0) {
        for (int i = 0; i < MAX_OPS; i++) {
            deleteMessage(n->receivedVals[i]);
        }
        free(n->receivedVals);
    }
    free(n);
}
