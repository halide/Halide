#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <assert.h>

typedef enum {Load, Store, Compute, Allocate, Free, Produce} event_type;

struct event {
    int location[4];
    int size[4];
    char name[64];
    event_type type;
};

bool next_token(char **first, char **rest) {
    (*first) = (*rest);
    if ((*first)[0] == 0 || (*first)[0] == '\n') return false;
    for (int i = 0; ; i++) {
        if ((*first)[i] == ' ' || (*first)[i] == '\n' || (*first)[i] == 0) {
            if ((*first)[i]) {
                (*rest) = (*first) + i + 1;
            } else {
                (*rest) = (*first) + i;
            }
            (*first)[i] = 0;
            return true;
        }
    }
}

int parse_ints(char **args, int *result) {
    char *arg;
    if (!next_token(&arg, args)) return 0;
    if (arg[0] == '\n' || arg[0] == ']') return 0;
    if (arg[0] == '[') {
        result[0] = atoi(arg+1);
        int count = 1;
        while (1) {
            next_token(&arg, args);
            result[count++] = atoi(arg);
            if (arg[strlen(arg)-1] == ']') return count;
        }
    } else {
        result[0] = atoi(arg);
        return 1;
    }
}

void parse_multi_ints(char **args, int **result, int *width, int *height) {
    int sizes[8];

    *width = 0;
    for (int i = 0; i < 8; i++) {
        sizes[i] = parse_ints(args, result[i]);
        if (sizes[i] > *width) *width = sizes[i];
        if (sizes[i] == 0) {
            *height = i;
            break;
        }
    }

    for (int i = 0; i < *height; i++) {
        if (sizes[i] < *width) {
            assert(sizes[i] == 1);
            for (int j = 0; j < *width; j++) {
                result[i][j] = result[i][0];
            }           
        }
    }    
}

int parse_event_location(char *args, event *events) {
    char *arg;
    int result[8][32];
    int *result_ptrs[8];
    for (int i = 0; i < 8; i++) {
        result_ptrs[i] = &(result[i][0]);
    }

    memset(events, 0, sizeof(event)*32);

    int width, height;
    next_token(&arg, &args);
    if (strncmp(arg, "at", 2) == 0) {
        parse_multi_ints(&args, &(result_ptrs[0]), &width, &height);
        assert(height <= 4);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {                
                events[x].location[y] = result[y][x];
                events[x].size[y] = 1;
            }
        }

    } else if (strncmp(arg, "over", 4) == 0) {
        parse_multi_ints(&args, &(result_ptrs[0]), &width, &height);
        assert(height <= 8);
        for (int y = 0; y < height/2; y++) {
            for (int x = 0; x < width; x++) {                
                events[x].location[y] = result[y][x];
            }
        }
        for (int y = height/2; y < height; y++) {
            for (int x = 0; x < width; x++) {                
                events[x].size[y-height/2] = result[y][x];
            }
        }
    } else {
        printf("Did not understand event location: %s\n", args);
    }

    return width;
}

void parse_log(const char *filename, std::vector<event> &log) {
    // Parse an event log
    FILE *f = fopen(filename, "r");

    event new_events[32];
    char buf[1024];
    char *args;
    while (fgets(buf, 1023, f)) {
        args = &buf[0];
        event_type t;
        char *type;
        next_token(&type, &args);
        if (strncmp(type, "Loading", 7) == 0) t = Load;
        else if (strncmp(type, "Storing", 7) == 0) t = Store;
        else if (strncmp(type, "Computing", 9) == 0) t = Compute;
        else if (strncmp(type, "Allocating", 10) == 0) t = Allocate;
        else if (strncmp(type, "Freeing", 7) == 0) t = Free;
        else if (strncmp(type, "Producing", 9) == 0) t = Produce;
        else continue;

        char *buffer;
        next_token(&buffer, &args);
        int num_new_events = parse_event_location(args, &(new_events[0]));
        for (int i = 0; i < num_new_events; i++) {
            event e = new_events[i];
            e.name[0] = 0;
            strncat(&(e.name[0]), buffer, 63);
            e.type = t;
            log.push_back(e);
        }
    }    

    fclose(f);
}

#include <GL/glew.h>
#include <GL/glut.h>
#include <GL/freeglut_ext.h>

int width = 1600;
int height = 1200;

std::vector<event> log;
int log_idx = 0;

struct buffer_pos {
    int x, y;
    char name[64];
};
std::vector<buffer_pos> positions;

void init() {

    glutReshapeWindow(width, height);

    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}


void clear() {
    glClear(GL_COLOR_BUFFER_BIT);

    glColor3f(1, 1, 1);
    for (size_t i = 0; i < positions.size(); i++) {
        glRasterPos2i(positions[i].x*4, positions[i].y*4-10);
        glutBitmapString(GLUT_BITMAP_HELVETICA_18, (const unsigned char *)positions[i].name);
    }
}

void draw_events() {
    glDisable(GL_TEXTURE_2D);

    glPointSize(4);

    glBegin(GL_QUADS);
    log_idx -= 1000;
    if (log_idx < 0) log_idx = 0;
    for (int i = 0; i < 1025; i++) {        
        float fade = i/1025.0f;
        fade *= 2;
        fade -= 0.5;
        if (fade < 0) fade = 0;
        if (fade > 1) fade = 1;
        event &e = log[log_idx];

        int x_off = -100000, y_off = -100000;
        for (size_t j = 0; j < positions.size(); j++) {
            if (strncmp(e.name, positions[j].name, 64) == 0) {
                x_off = positions[j].x;
                y_off = positions[j].y;
            }
        }
        log_idx++;
        if (log_idx >= (int)log.size()) {
            glEnd();
            clear();
            log_idx = 0;
            glBegin(GL_QUADS);
        }

        if (e.type == Load) {
            glColor4f(0, fade*0.5+0.5, 0, 1);
        } else if (e.type == Store) {
            glColor4f(fade*0.5+0.5, 0, 0, 1);
        } else if (e.type == Allocate) {
            glColor4f(0.2, 0.2, 0.8, 1);
        } else if (e.type == Free) {
            glColor4f(0.2, 0.2, 0.2, 1);
        } else {
            continue;
        }

        int x = e.location[0] + x_off;
        int y = e.location[1] + y_off;
        glVertex3i(4*x, 4*y, 0);
        x += e.size[0];
        glVertex3i(4*x, 4*y, 0);
        y += e.size[1];
        glVertex3i(4*x, 4*y, 0);
        x -= e.size[0];
        glVertex3i(4*x, 4*y, 0);
    }
    glEnd();    
}

void display() {    

    draw_events();

    glutSwapBuffers();
    usleep(16000);
}

void reshape(int w, int h) {
    printf("%d %d\n", w, h);
    width = w; height = h;
    log_idx = 0;

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glScalef(2.0/width, -2.0/height, 0.1);
    glTranslatef(-width/2, -height/2, 0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();    

    clear();

}

int main(int argc, char **argv) {
    
    parse_log(argv[1], log);

    for (int i = 2; i < argc-2; i+=3) {
        buffer_pos p;
        p.name[0] = 0;
        strncat(p.name, argv[i], 63);
        p.x = atoi(argv[i+1]);
        p.y = atoi(argv[i+2]);
        positions.push_back(p);
        printf("%s at %d %d\n", p.name, p.x, p.y);
    }

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGBA);

    glutCreateWindow("Viz");

    glutDisplayFunc(display);
    glutIdleFunc(glutPostRedisplay);    

    glutReshapeFunc(reshape);

    glewInit();

    init();
    glutMainLoop();    
}
