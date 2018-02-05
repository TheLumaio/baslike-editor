#include "script.h"

void execute(baslike_t* script, char* text)
{
    reset(script);
    populate(script, text);
    preprocess(script);
    for (script->opindex = 0; script->opindex < script->stacksize; script->opindex++)
    {
        int op = isop(script->stack[script->opindex]);
        doop(script, op);
        if (script->failed) break;
    }
}

void reset(baslike_t* script) {
    for (int i = 0; i < script->stacksize; i++)
        memset(script->stack[i], '\0', 16);
    for (int i = 0; i < MEM; i++)
        script->memory[i] = 0;
    for (int i = 0; i < script->labelsize; i++)
        script->labels[i] = -1;
    script->stacksize = 0;
    script->labelsize = 0;
    script->opindex = 0;
    script->mds = 0;
    script->mdx = 0;
    memset(script->output, '\0', 1024);
}

void preprocess(baslike_t* script)
{
    for (int i = 0; i < script->stacksize; i++) {
        if (isop(script->stack[i]) == OP_DEF) {
            script->labels[script->labelsize] = i+1;
            script->labelsize++;
        }
    }
}

void populate(baslike_t* script, char* text)
{
    int i;
    for (i=0;i<512;i++)memset(script->stack[i], '\0', 16);
    for (i=0;i<strlen(text);i++)if(text[i]=='\n')text[i]=' ';
    int index = 0;
    char *token = strtok(text, " \n");
    while(token) {
        bool ignore = false;
        for (i=0;i<strlen(token);i++) {
            if (islower(token[i])) {
                ignore = true;
                break;
            }
        }
        if (!ignore) {
            strcpy(script->stack[index], token);
            index++;
        }
        token = strtok(NULL, " ");
    }
    free(token);
    script->stacksize = index;
}

int isop(char* op)
{
    for (int i = 0; i < OPS; i++)
        if (!strcmp(op, ops[i])) return i;
    return OP_NON;
}

void doop(baslike_t* script, int op)
{
    switch (op)
    {
        case OP_NON: {
            scriptoutput(script->output, "NON OPERATION (%s:%d)\n", script->stack[script->opindex], script->opindex);
            script->failed=true;
        } break;
        case OP_MDS: {
            if (isop(script->stack[script->opindex+1]) == OP_MDX) script->mds = script->memory[script->mdx];
            else script->mds = atoi(script->stack[script->opindex+1]);
            script->opindex++;
        } break;
        case OP_IFE: {
            int els = -1;
            int enf = -1;
            for (int i = script->opindex; i < script->stacksize; i++) {
                int eop = isop(script->stack[i]);
                if (eop == OP_ENF) {
                    enf = i;
                    break;
                }
                if (eop == OP_ELS) {
                    els = i;
                }
            }
            if (enf == -1) {
                scriptoutput(script->output, "ERROR: NO ENF\n");
                script->failed=true;
                break;
            }
            int m;
            bool jumped=false;
            if (isop(script->stack[script->opindex+1]) == OP_MDX) m = script->memory[script->mdx];
            else m = atoi(script->stack[script->opindex+1]);
            if (script->memory[script->mds] == m) {
                script->opindex+=2;
                int to = els > -1 ? els : enf;
                for (; script->opindex < to; script->opindex++) {
                    if (isop(script->stack[script->opindex])==OP_JMP) {
                        doop(script, isop(script->stack[script->opindex]));
                        jumped=true;
                        break;
                    }
                    doop(script, isop(script->stack[script->opindex]));
                    if (script->failed) break;
                }
            } else {
                if (els > -1) {
                    script->opindex = els;
                    for (; script->opindex < enf; script->opindex++) {
                        if (isop(script->stack[script->opindex])==OP_JMP) {
                            doop(script, isop(script->stack[script->opindex]));
                            jumped=true;
                            break;
                        }
                        doop(script, isop(script->stack[script->opindex]));
                        if (script->failed) break;
                    }
                }
            }
            if (!jumped)
                script->opindex = enf;
        } break;
        case OP_SET: {
            int setop = isop(script->stack[script->opindex+1]);
            if (setop == OP_MDX)
                script->memory[script->mds] = script->memory[script->mdx];
            else
                script->memory[script->mds] = atoi(script->stack[script->opindex+1]);
            script->opindex++;
        } break;
        case OP_ADD: {
            int addop = isop(script->stack[script->opindex+1]);
            if (addop == OP_MDX)
                script->memory[script->mds] += script->memory[script->mdx];
            else
                script->memory[script->mds] += atoi(script->stack[script->opindex+1]);
            script->opindex++;
        } break;
        case OP_ENF: {
            
        } break;
        case OP_PRN: {
            if (isop(script->stack[script->opindex+1]) == OP_MDX)
                scriptoutput(script->output, "MDX: %d\n", script->memory[script->mdx]);
            else if (isop(script->stack[script->opindex+1]) == OP_MDS)
                scriptoutput(script->output, "MDS: %d\n", script->memory[script->mds]);
            else
                scriptoutput(script->output, "OUT: %s\n", script->stack[script->opindex+1]);
            script->opindex++;
        } break;
        case OP_ELS: {
        } break;
        case OP_MEM: {
            scriptoutput(script->output, "MEM: ");
            for (int i = 0; i < MEM; i++) {
                scriptoutput(script->output, "%d ", script->memory[i]);
            }
            scriptoutput(script->output, "\n");
        } break;
        case OP_DEF: {
            script->opindex++;
        } break;
        case OP_JMP: {
            bool found=false;
            for (int i = 0; i < script->labelsize; i++) {
                if (!strcmp(script->stack[script->labels[i]], script->stack[script->opindex+1])) {
                    script->opindex = script->labels[i];
                    found=true;
                    break;
                }
            }
            if (!found) {
                scriptoutput(script->output, "ERROR: NO LABEL (%s)\n", script->stack[script->opindex+1]);
                script->failed=true;
                break;
            }
        } break;
        case OP_IFL: {
            int els = -1;
            int enf = -1;
            for (int i = script->opindex; i < script->stacksize; i++) {
                int eop = isop(script->stack[i]);
                if (eop == OP_ENF) {
                    enf = i;
                    break;
                }
                if (eop == OP_ELS) {
                    els = i;
                }
            }
            if (enf == -1) {
                scriptoutput(script->output, "ERROR: NO ENF\n");
                script->failed=true;
                break;
            }
            int m;
            bool jumped=false;
            if (isop(script->stack[script->opindex+1]) == OP_MDX) m = script->memory[script->mdx];
            else m = atoi(script->stack[script->opindex+1]);
            if (script->memory[script->mds] < m) {
                script->opindex+=2;
                int to = els > -1 ? els : enf;
                for (; script->opindex < to; script->opindex++) {
                    if (isop(script->stack[script->opindex])==OP_JMP) {
                        doop(script, isop(script->stack[script->opindex]));
                        jumped=true;
                        break;
                    }
                    doop(script, isop(script->stack[script->opindex]));
                    if (script->failed) break;
                }
            } else {
                if (els > -1) {
                    script->opindex = els;
                    for (; script->opindex < enf; script->opindex++) {
                        if (isop(script->stack[script->opindex])==OP_JMP) {
                            doop(script, isop(script->stack[script->opindex]));
                            jumped=true;
                            break;
                        }
                        doop(script, isop(script->stack[script->opindex]));
                        if (script->failed) break;
                    }
                }
            }
            if (!jumped)
                script->opindex = enf;
        } break;
        case OP_IFG: {
            int els = -1;
            int enf = -1;
            for (int i = script->opindex; i < script->stacksize; i++) {
                int eop = isop(script->stack[i]);
                if (eop == OP_ENF) {
                    enf = i;
                    break;
                }
                if (eop == OP_ELS) {
                    els = i;
                }
            }
            if (enf == -1) {
                scriptoutput(script->output, "ERROR: NO ENF\n");
                script->failed=true;
                break;
            }
            int m;
            bool jumped=false;
            if (isop(script->stack[script->opindex+1]) == OP_MDX) m = script->memory[script->mdx];
            else m = atoi(script->stack[script->opindex+1]);
            if (script->memory[script->mds] > m) {
                script->opindex+=2;
                int to = els > -1 ? els : enf;
                for (; script->opindex < to; script->opindex++) {
                    if (isop(script->stack[script->opindex])==OP_JMP) {
                        doop(script, isop(script->stack[script->opindex]));
                        jumped=true;
                        break;
                    }
                    doop(script, isop(script->stack[script->opindex]));
                    if (script->failed) break;
                }
            } else {
                if (els > -1) {
                    script->opindex = els;
                    for (; script->opindex < enf; script->opindex++) {
                        if (isop(script->stack[script->opindex])==OP_JMP) {
                            doop(script, isop(script->stack[script->opindex]));
                            jumped=true;
                            break;
                        }
                        doop(script, isop(script->stack[script->opindex]));
                        if (script->failed) break;
                    }
                }
            }
            if (!jumped)
                script->opindex = enf;
        } break;
        case OP_MDX: {
            script->mdx = atoi(script->stack[script->opindex+1]);
            script->opindex++;
        } break;
        case OP_NEG: {
            script->memory[script->mds] = -script->memory[script->mds];
        } break;
        case OP_MUL: {
            int addop = isop(script->stack[script->opindex+1]);
            if (addop == OP_MDX)
                script->memory[script->mds] *= script->memory[script->mdx];
            else
                script->memory[script->mds] *= atoi(script->stack[script->opindex+1]);
            script->opindex++;
        } break;
        case OP_DIV: {
            int addop = isop(script->stack[script->opindex+1]);
            if (addop == OP_MDX)
                script->memory[script->mds] /= script->memory[script->mdx];
            else
                script->memory[script->mds] /= atoi(script->stack[script->opindex+1]);
            script->opindex++;
        } break;
        default: {
            scriptoutput(script->output, "UNDEFINED OPERATION (%s)\n", script->stack[script->opindex]);
        } break;
    }
}