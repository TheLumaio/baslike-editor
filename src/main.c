#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <raylib.h>
#include "script.h"
#include "gui.h"

#define MAXLINES 999
#define MAXLENGTH 64
#define DRAWMAX 35
static int startline = 0;
static char lines[MAXLINES][MAXLENGTH];
static int numlines=1;
static Vector2 cursorpos = {0,0};
static int cursorop = -1;

static SpriteFont font;
static int WIDTH;
static int HEIGHT;
static baslike_t script;
static bool script_running = false;
static pthread_t script_thread;

static Color ogreen1 = (Color){100, 125, 100, 255};
static Color ogreen2 = (Color){75, 255, 75, 255};
static Color ored1 = (Color){125, 100, 100, 255};
static Color ored2 = (Color){255, 75, 75, 255};
static Color ogray1 = (Color){100, 100, 100, 255};
static Color ogray2 = (Color){125, 125, 125, 255};

static bool create_new=false;
static bool editing_save=false;
static char file_save[23] = "";

static Color syntax[OPS+1] = {
    (Color){100, 100, 100, 255}, // non
    (Color){200, 200, 100, 255}, // mds
    (Color){200, 200, 100, 255}, // mdx
    (Color){200, 100, 100, 255}, // ife
    (Color){200, 100, 100, 255}, // ifl
    (Color){200, 100, 100, 255}, // ifg
    (Color){200, 100, 100, 255}, // els
    (Color){200, 100, 100, 255}, // enf
    (Color){100, 200, 200, 255}, // set
    (Color){100, 200, 200, 255}, // add
    (Color){100, 200, 200, 255}, // mul
    (Color){100, 200, 200, 255}, // div
    (Color){100, 200, 200, 255}, // neg
    (Color){100, 100, 200, 255}, // prn
    (Color){100, 100, 200, 255}, // mem
    (Color){100, 200, 100, 255}, // def
    (Color){100, 200, 100, 255}, // jmp
    (Color){100, 200, 100, 255}, // fnc
    (Color){100, 200, 100, 255}, // end
    (Color){100, 200, 100, 255}, // cal
    (Color){100, 200, 150, 255}, // ret
    (Color){100, 200, 150, 255}, // push
    (Color){100, 200, 150, 255}, // pop
};

void DrawTextB(const char* text, int x, int y, int size, Color color)
{
    DrawTextEx(font, text, (Vector2){x,y}, size, 2, color);
}

void append(char subject[], const char insert[], int pos) {
    char buf[MAXLENGTH] = {};
    strncpy(buf, subject, pos);
    int len = strlen(buf);
    strcpy(buf+len, insert);
    len += strlen(insert);
    strcpy(buf+len, subject+pos);

    strcpy(subject, buf);
}

void* runscript(void* code) {
    script_running = true;
    execute(&script, code);
    script_running = false;
    return NULL;
}

void dump_text(char* file) {
    FILE* fp;
    fp = fopen(file, "w");
    for (int i = 0; i < numlines; i++) {
        fprintf(fp, "%s", lines[i]);
        if (i < numlines-1)
            fprintf(fp, "\n");
    }
    fclose(fp);
}

static bool comment = false;

void handle_input();
void handle_save();
void handle_new();

int test_funct(baslike_t* s) {
    scriptoutput(s, "TEST FUNCT\n");
    return -1;
}

int hash_funct(baslike_t* s) {
    int x = s->args[0];
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    s->ret = x;
    return -1;
}

int main(int argc, char** argv)
{
    InitWindow(640, 480, "BASLIKE 0.9.4");
    SetTargetFPS(60);
    SetExitKey(KEY_F12);
    
    font = LoadSpriteFont("ProggyClean.ttf");
    WIDTH = MeasureTextEx(font, " ", 13, 0).x+2;
    HEIGHT = MeasureTextEx(font, " ", 13, 0).y;
    
    reset(&script);
    linkfunction(&script, test_funct, "TEST");
    linkfunction(&script, hash_funct, "HASH");
    
    while (!WindowShouldClose()) {
        handle_input();
        handle_save();
        handle_new();
        
        if (IsKeyDown(KEY_LEFT_CONTROL)) {
            if (IsKeyPressed(KEY_S) && !editing_save && !create_new) {
                editing_save = true;
            }
            if (IsKeyPressed(KEY_N) && !create_new && !editing_save) {
                create_new = true;
            }
        }
        
        if (IsFileDropped()) {
            int count;
            char** files = GetDroppedFiles(&count);
            for (int i = 0; i < MAXLINES; i++) { memset(lines[i], '\0', MAXLENGTH); }
            numlines = 1;
            FILE* fp;
            char buffer[MAXLENGTH];
            fp = fopen(files[0], "r");
            int i = 0;
            while (fgets(buffer, sizeof(buffer), fp) != NULL) {
                if (buffer[strlen(buffer)-1] == '\n')
                    buffer[strlen(buffer)-1] = '\0';
                strcpy(lines[i], buffer);
                i++;
                numlines++;
            }
            fclose(fp);
            strcpy(file_save, GetFileName(files[0]));
            ClearDroppedFiles();
        }
        
        ClearBackground(BLACK);
        BeginDrawing();
            comment = false;
            cursorop = -1;
            int eindex = 0;
            for (int i = 0; i < DRAWMAX; i++) {
                char* l = strlen(lines[startline+i])>0 ? lines[startline+i] : i+startline<numlines?"":"~";
                DrawTextB(FormatText("%03d: %s", startline+i, l), 10, 10+i*13, 13, syntax[0]);
                if (strcmp(l, "~") == 0) continue;
                
                // syntax highlighter
                char* str = (char*)malloc(sizeof(char)*strlen(l)+1);
                strcpy(str, l);
                
                int num = 0;
                char tokens[512][32];
                char line[32];
                memset(line, '\0', 32);
                int li = 0;
                int state = 0; // 0:dull, 1:string, 2:comment
                int stacksize = 0;
                if (comment) state = 2;
                while (*str) {
                    switch (state)
                    {
                        case 0:
                            if (*str == '"') {
                                state = 1;
                                if (strlen(line) > 0) {
                                    strcpy(tokens[num], line);
                                    num++;
                                    memset(line, '\0', 32);
                                    li = 0;
                                }
                                
                                line[li] = *str;
                                li++;
                            }
                            else if (*str == ' ') {
                                if (strlen(line) > 0) {
                                    strcpy(tokens[num], line);
                                    num++;
                                    memset(line, '\0', 32);
                                    li = 0;
                                }
                            }
                            else if (*str == '(') {
                                state = 2;
                                if (strlen(line) > 0) {
                                    strcpy(tokens[num], line);
                                    num++;
                                    memset(line, '\0', 32);
                                    li = 0;
                                }
                            }
                            else {
                                line[li] = *str;
                                li++;
                            }
                            break;
                        case 1:
                            if (*str == '"') {
                                state = 0;
                                line[li] = *str;
                                li++;
                                if (strlen(line) > 0) {
                                    strcpy(tokens[num], line);
                                    num++;
                                    memset(line, '\0', 32);
                                    li = 0;
                                }
                            }
                            else {
                                line[li] = *str;
                                li++;
                            }
                            break;
                        case 2:
                            if (*str == ')') {
                                state = 0;
                            }
                            *str++;
                            break;
                        default:
                            break;
                    }
                    *str++;
                }
                if (strlen(line) > 0) {
                    strcpy(tokens[num], line);
                    num++;
                }
                int size = 0;
                int op = -1;
                for (int j = 0; j < num; j++) {
                    op = isop(tokens[j]);
                    for (int k = size; k < strlen(l); k++) {
                        if (l[k] == ' ') size++;
                        else break;
                    }
                    Color c = syntax[op+1];
                    if (j > 0) {
                        int lop = isop(tokens[j-1]); 
                        if ((lop != OP_NON && op == OP_NON) || (tokens[j][0] == '<' && tokens[j][strlen(tokens[j])-1] == '>')) {
                            c = (Color){200, 200, 200, 255};
                        }
                    }
                    if (script.failed && script.error > -1 && script.error == eindex) {
                        DrawRectangle(10+WIDTH*5+WIDTH*size, 10+i*13, WIDTH*strlen(tokens[j]), 11, (Color){255, 0, 0, 200});
                    }
                    DrawTextB(FormatText("%s", tokens[j]), 10+WIDTH*5+WIDTH*size, 10+i*13, 13, c);
                    if (cursorpos.y+startline == i && cursorpos.x > size-1 && cursorpos.x < size+strlen(tokens[j])) {
                        cursorop = eindex;
                    }
                    size += strlen(tokens[j]);
                    eindex++;
                }
            }
            DrawRectangle(10+WIDTH*5+cursorpos.x*WIDTH, 10+cursorpos.y*HEIGHT-1, WIDTH+1, HEIGHT+2, (Color){155, 155, 155, 155});
            
            DrawRectangle(640-160, 50, 155, 380, script_running?ogray1:script.failed?ored1:ogreen1);
            DrawRectangleLines(640-160, 50, 155, 380, script_running?ogray2:script.failed?ored2:ogreen2);
            DrawTextB(script.output, 640-150, 60, 13, WHITE);
            
            DrawRectangle(640-260, 50, 75, 180, script_running?ogray1:script.failed?ored1:ogreen1);
            DrawRectangleLines(640-260, 50, 75, 180, script_running?ogray2:script.failed?ored2:ogreen2);
            for (int i = 0; i < script.labelsize; i++) {
                DrawTextB(FormatText("%d:%s", script.labels[i], script.stack[script.labels[i]]), 640-250, 60+i*13, 13, WHITE);
            }
            
            DrawRectangle(640-260, 250, 75, 180, script_running?ogray1:script.failed?ored1:ogreen1);
            DrawRectangleLines(640-260, 250, 75, 180, script_running?ogray2:script.failed?ored2:ogreen2);
            
            for (int i = 0; i < script.functionsize; i++) {
                DrawTextB(FormatText("%d:%s", script.functions[i].pos, script.stack[script.functions[i].pos]), 640-250, 260+i*13, 13, WHITE);
            }
            
            DrawTextB(FormatText("OP: %d", cursorop), 10, 480-15, 13, WHITE);
            
            char* mem_text = FormatText("[%d, %d, %d, %d, %d, %d, %d, %d]", script.memory[0], script.memory[1], script.memory[2], script.memory[3], script.memory[4], script.memory[5], script.memory[6], script.memory[7]);
            DrawRectangle(640-15-WIDTH*strlen(mem_text), 480-40, WIDTH*strlen(mem_text)+10, 20, DARKGRAY);
            DrawRectangleLines(640-15-WIDTH*strlen(mem_text), 480-40, WIDTH*strlen(mem_text)+10, 20, WHITE);
            DrawTextB(mem_text, 640-10-WIDTH*strlen(mem_text), 480-35, 13, WHITE);
            
            gui_label("Output", 640-80-80, 30, 75, 25);
            gui_label("Labels", 640-270, 30, 75, 25);
            gui_label("Functions", 640-260, 230, 75, 25);
            if (gui_button("Execute", 640-80-80, 5, 75, 25) && !editing_save) {
                if (script_running)
		    pthread_cancel(script_thread);
                reset(&script);
                script.failed = true;
                script_running = false;
                char code[MAXLINES*MAXLENGTH] = "";
                for (int i = 0; i < numlines; i++) {
                    strcat(code, lines[i]);
                    strcat(code, "\n");
                }
                pthread_create(&script_thread, NULL, runscript, &code);
            }
            if (gui_button("Cancel", 640-80, 5, 75, 25) && !editing_save && script_running) {
                pthread_cancel(script_thread);
                script.failed = true;
                script_running = false;
            }
            
            if (editing_save) {
                if (IsKeyPressed(KEY_ESCAPE)) editing_save = false;
                DrawRectangle(0, 0, 640, 480, (Color){10, 10, 10, 200});
                DrawRectangle(640/2-100, 480/2-30, 200, 25, (Color){100, 100, 100, 255});
                DrawRectangleLines(640/2-100, 480/2-30, 200, 25, (Color){255, 255, 255, 255});
                DrawTextB(file_save, 640/2-95, 480/2-23, 13, WHITE);
                DrawRectangle(640/2-95+WIDTH*strlen(file_save), 480/2-23, WIDTH, HEIGHT, GRAY);
            }
            if (create_new) {
                DrawRectangle(0, 0, 640, 480, (Color){10, 10, 10, 200});
                DrawRectangle(640/2-100, 480/2-30, 200, 25, (Color){100, 100, 100, 255});
                DrawRectangleLines(640/2-100, 480/2-30, 200, 25, (Color){255, 255, 255, 255});
                DrawTextB("Are you sure you? Y/N", 640/2-95, 480/2-23, 13, WHITE);
            }
        EndDrawing();
        
    }
    
    return 0;
}

void handle_new()
{
    if (!create_new) return;
    if (IsKeyPressed(KEY_Y)) {
        for (int i = 0; i < MAXLINES; i++) { memset(lines[i], '\0', MAXLENGTH); }
        numlines = 1;
        create_new = false;
        cursorpos.x = 0;
        cursorpos.y = 0;
    }
    if (IsKeyPressed(KEY_N) || IsKeyPressed(KEY_ESCAPE)) {
        create_new = false;
    }
}

void handle_save()
{
    if (!editing_save) return;
    int c = GetKeyPressed();
    if (c!=-1 && strlen(file_save) < 23) {
        file_save[strlen(file_save)] = c;
    }
    if (IsKeyPressed(KEY_BACKSPACE)) {
        if (strlen(file_save) > 0) {
            file_save[strlen(file_save)-1] = '\0';
        }
    }
    if (IsKeyPressed(KEY_ENTER)) {
        dump_text(file_save);
        editing_save = false;
    }
}

void handle_input()
{
    if (editing_save || create_new) return;
    int c = GetKeyPressed();
    if (c!=-1 && strlen(lines[(int)cursorpos.y+startline]) < MAXLENGTH && c!=KEY_TAB) {
        script.error = -1;
        memmove(
            lines[(int)cursorpos.y+startline]+(int)cursorpos.x+1,
            lines[(int)cursorpos.y+startline]+(int)cursorpos.x,
            MAXLENGTH - ((int)cursorpos.x+1)
        );
        lines[(int)cursorpos.y+startline][(int)cursorpos.x] = c;
        cursorpos.x++;
    }
    if (IsKeyPressed(KEY_BACKSPACE)) {
        script.error = -1;
        if (cursorpos.x > 0) {
            int n = 0;
            for (int i = cursorpos.x; i > 0; i--) {
                if (lines[(int)cursorpos.y+startline][i-1] == ' ')
                    n++;
                else
                    break;
            }
            if ((int)cursorpos.x%4==0 && n>=4) {
                for (int i = 0; i < 4; i++) {
                    for(int i = (int)cursorpos.x-1; i < MAXLENGTH - 1; i++) lines[(int)cursorpos.y+startline][i] = lines[(int)cursorpos.y+startline][i + 1];
                    cursorpos.x--;
                }
            } else {
                for(int i = (int)cursorpos.x-1; i  < MAXLENGTH - 1; i++) lines[(int)cursorpos.y+startline][i] = lines[(int)cursorpos.y+startline][i + 1];
                cursorpos.x--;
            }
        }
        else if (strlen(lines[(int)cursorpos.y+startline]) == 0 && cursorpos.y > 0) {
            for (int i = cursorpos.y+startline; i < MAXLINES-1; i++) {
                strcpy(lines[i], lines[i+1]);
            }
            
            if (cursorpos.y < 2 && startline > 0){ startline--; } else { cursorpos.y--; }
            cursorpos.x = strlen(lines[(int)cursorpos.y+startline]);
            numlines--;
        }
    }
    if (IsKeyPressed(KEY_ENTER) && cursorpos.x<MAXLINES) {
        script.error = -1;
        for (int i = MAXLINES-1; i > cursorpos.y+startline+1; i--) {
            strcpy(lines[i], lines[i-1]);
        }
        if (cursorpos.y > 30){ startline++; } else { cursorpos.y++; }
        memset(lines[(int)cursorpos.y+startline], '\0', MAXLENGTH);
        int indent = 0;
        for (int i = 0; i < strlen(lines[(int)cursorpos.y+startline-1]); i++)
            if (lines[(int)cursorpos.y+startline-1][i] == ' ')
                indent++;
            else
                break;
        for (int i = 0; i < indent; i++) {
            lines[(int)cursorpos.y+startline][i] = ' ';
            cursorpos.x++;
        }
        if (cursorpos.x > strlen(lines[(int)cursorpos.y+startline])) cursorpos.x = strlen(lines[(int)cursorpos.y+startline]);
        numlines++;
    }
    if (IsKeyPressed(KEY_LEFT) && cursorpos.x > 0) {
        cursorpos.x--;
    }
    if (IsKeyPressed(KEY_RIGHT) && cursorpos.x < strlen(lines[(int)cursorpos.y+startline])) {
        cursorpos.x++;
    }
    if (IsKeyPressed(KEY_UP) && cursorpos.y+startline > 0) {
        if (cursorpos.y < 1){ startline--; } else { cursorpos.y--; }
        if (cursorpos.x > strlen(lines[(int)cursorpos.y+startline])) cursorpos.x = strlen(lines[(int)cursorpos.y+startline]);
    }
    if (IsKeyPressed(KEY_DOWN) && cursorpos.y+startline < numlines-1) {
        if (cursorpos.y > 30){ startline++; } else { cursorpos.y++; }
        if (cursorpos.x > strlen(lines[(int)cursorpos.y+startline])) cursorpos.x = strlen(lines[(int)cursorpos.y+startline]);
    }
    if (IsKeyPressed(KEY_TAB) && strlen(lines[(int)cursorpos.y+startline]) < MAXLENGTH-4) {
        script.error = -1;
        for (int i = 0; i < 4; i++) {
            memmove(
                lines[(int)cursorpos.y+startline]+(int)cursorpos.x+1,
                lines[(int)cursorpos.y+startline]+(int)cursorpos.x,
                MAXLENGTH - ((int)cursorpos.x+1)
            );
            lines[(int)cursorpos.y+startline][(int)cursorpos.x] = ' ';
            cursorpos.x++;
        }
    }
    if (IsKeyPressed(KEY_HOME)) {
        cursorpos.x = 0;
    }
    if (IsKeyPressed(KEY_END)) {
        cursorpos.x = strlen(lines[(int)cursorpos.y+startline]);
    }
}
