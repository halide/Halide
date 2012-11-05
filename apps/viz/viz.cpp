#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <assert.h>
#include "../../support/static_image.h"
#include "../../support/image_io.h"

typedef enum {Load, Store, Compute, Allocate, Free, Produce} event_type;

void __copy_to_host(buffer_t* buf) {}

struct event {
    int location[4];
    int size[4];
    char name[64];
    event_type type;
    bool chained;
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
                events[x].chained = (x < width-1);
            }
        }

    } else if (strncmp(arg, "over", 4) == 0) {
        parse_multi_ints(&args, &(result_ptrs[0]), &width, &height);
        assert(height <= 8);
        for (int y = 0; y < height/2; y++) {
            for (int x = 0; x < width; x++) {                
                events[x].location[y] = result[y][x];
                events[x].chained = (x < width-1);
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
        /* else if (strncmp(type, "Computing", 9) == 0) t = Compute; */
        else if (strncmp(type, "Allocating", 10) == 0) t = Allocate;
        else if (strncmp(type, "Freeing", 7) == 0) t = Free;
        /* else if (strncmp(type, "Producing", 9) == 0) t = Produce; */
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

#include <GL/glut.h>
#include <GL/freeglut_ext.h>

int width = 1600;
int height = 1200;

int speed = 32;
bool single_step = true;

bool screenshot = false;
bool record_movie = false;
int movie_frame_counter = 0;

const char *log_filename;
std::vector<event> log;
int log_idx = 0, log_tail = 0;



struct buffer_pos {
    int x, y;
    int zoom;
    char name[64];
    char caption[64];
};
std::vector<buffer_pos> positions;

void keyboardEvent(unsigned char key, int x, int y) {
    if (key == '+') {
        speed *= 2;
        if (speed == 0) speed = 1;
    } else if (key == '-') {
        speed /= 2;
    } else if (key == 'r') {
        log_idx = 0;
        log_tail = 0;
    } else if (key == ' ') {
        single_step = true;
    } else if (key == 's') {
        screenshot = true;
    } else if (key == 'm') {
        movie_frame_counter = 0;
        record_movie = !record_movie;
    }
}

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
        glRasterPos2i(positions[i].x, positions[i].y-10 - positions[i].zoom);
        glutBitmapString(GLUT_BITMAP_HELVETICA_18, (const unsigned char *)positions[i].caption);
    }
}

void draw_events() {
    glDisable(GL_TEXTURE_2D);

    glPointSize(4);

    glBegin(GL_QUADS);

    int new_log_tail = std::min(log_tail + speed, log_idx - (speed+1)*5);
    int new_log_idx = log_idx + speed;

    if (single_step) {
        new_log_idx = log_idx+1;
        while (new_log_idx >= 0 && 
               new_log_idx < (int)log.size() &&
               log[new_log_idx].chained) new_log_idx++;        
        single_step = false;
        speed = 0;
   }

    for (int i = log_tail; i <= new_log_idx && i < (int)log.size(); i++) {        
        if (i < 0) continue;
        if (i == 0) {
            glEnd();
            clear();
            glBegin(GL_QUADS);
        }

        // log_tail through log_tail + speed should be cleaned up

        int j = i;
        while (i > new_log_tail && j < (int)log.size() && log[j].chained) j++;
        float fade = ((float)j - new_log_tail) / (new_log_idx - new_log_tail);
        if (fade > 1) fade = 1;
        if (fade < 0) fade = 0;
        if (j < log_idx) fade *= 0.25;

        event &e = log[i];

        int x_off = -100000, y_off = -100000;
        int zoom = 1;
        for (size_t j = 0; j < positions.size(); j++) {
            if (strncmp(e.name, positions[j].name, 64) == 0) {
                x_off = positions[j].x;
                y_off = positions[j].y;
                zoom = positions[j].zoom;
            }
        }
        
        
        for (int m = 0; m < (zoom > 4 ? 2 : 1); m++) {

            float r = 0, g = 0, b = 0;
            if (e.type == Load) {
                g = fade*0.5 + 0.5;
                b = 0.1 + 0.1*fade;
            } else if (e.type == Store) {
                r = fade*0.5 + 0.5;
                g = 0.15 + 0.15*fade;
            } else if (e.type == Allocate) {
                r = 0.2;
                g = 0.2;
                b = 0.4;
            } else if (e.type == Free) {
                r = g = b = 0.1;
            } else {
                continue;
            }            
            
            if (m == 0) {
                r /= 2;
                g /= 2;
                b /= 2;
            }

            glColor4f(r, g, b, 1);
            int margin = m * (zoom/5);
            int x = zoom*e.location[0] + x_off + margin;
            int y = zoom*e.location[1] + y_off + margin;
            glVertex3i(x, y, 0);
            x += e.size[0]*zoom - 2*margin;
            glVertex3i(x, y, 0);
            y += e.size[1]*zoom - 2*margin;
            glVertex3i(x, y, 0);
            x -= e.size[0]*zoom - 2*margin;
            glVertex3i(x, y, 0);
            r *= 2;
            g *= 2;
            b *= 2;
        }
    }
    glEnd();    

    log_tail = new_log_tail;
    log_idx = new_log_idx;
}

void display() {    

    draw_events();

    glutSwapBuffers();
    usleep(16000);

    if (screenshot || record_movie) {
        char buf[1024];
        if (screenshot) {
            snprintf(buf, 1024, "pics/%s_%05d.png", log_filename, log_idx);
            screenshot = false;
        } else {
            snprintf(buf, 1024, "pics/%s_movie_%05d.png", log_filename, movie_frame_counter++);
        }

        Image<uint8_t> im(width, height, 3);
        glReadPixels(0, 0, width, height, GL_RED, GL_UNSIGNED_BYTE, &im(0, 0, 0));
        glReadPixels(0, 0, width, height, GL_GREEN, GL_UNSIGNED_BYTE, &im(0, 0, 1));
        glReadPixels(0, 0, width, height, GL_BLUE, GL_UNSIGNED_BYTE, &im(0, 0, 2));

        for (int c = 0; c < 3; c++) {
            for (int y = 0; y < height/2; y++) {
                for (int x = 0; x < width; x++) {
                    std::swap(im(x, y, c), im(x, height-1-y, c));
                }
            }
        }

        save_png(im, buf);
        printf("Saved screenshot %s\n", buf);

    }
}

void reshape(int w, int h) {
    printf("%d %d\n", w, h);
    width = w; height = h;

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glScalef(2.0/width, -2.0/height, 0.1);
    glTranslatef(-width/2, -height/2, 0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();    

    clear();

    log_idx = 0;
}

int main(int argc, char **argv) {
    
    width = atoi(argv[1]);
    height = atoi(argv[2]);
    parse_log(argv[3], log);

    log_filename = argv[3];

    for (int i = 4; i < argc-4; i+=5) {
        buffer_pos p;
        p.name[0] = 0;
        strncat(p.name, argv[i], 63);
        p.caption[0] = 0;
        strncat(p.caption, argv[i+1], 63);
        p.x = atoi(argv[i+2]);
        p.y = atoi(argv[i+3]);
        p.zoom = atoi(argv[i+4]);
        positions.push_back(p);
        printf("%s at %d %d zoom %d\n", p.name, p.x, p.y, p.zoom);
    }

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGBA);

    glutCreateWindow("Viz");

    glutDisplayFunc(display);
    glutIdleFunc(glutPostRedisplay);    
    glutKeyboardFunc(keyboardEvent);

    glutReshapeFunc(reshape);

    init();
    glutMainLoop();    
}
