/* This is my ant killer */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include "types.h"
#include "list.h"
#include "list_sort.h"
#include "rbtree.h"

enum DIRECTION {N=0, E, S, W, MAX_DIRECTION};

static inline int max(int a, int b) 
{
	return (a > b) ? a : b;
}

struct pos {
	unsigned char row;
	unsigned char col;
};

struct path_step {
	struct pos pos;
	unsigned short distance;
	struct list_head node;
};

static LIST_HEAD(free_path_steps);
static struct path_step *alloc_path_step(void)
{
	struct path_step *step;
	if (!list_empty(&free_path_steps)) {
		step = container_of(free_path_steps.next, struct path_step, node);
		list_del_init(&step->node);
	} else {
		step = malloc(sizeof(*step));
		INIT_LIST_HEAD(&step->node);
	}
	return step;
}

struct path {
	union {
		struct target {
			struct pos start;
			struct pos end;
		} s;
		unsigned int value;
	} u;
	struct list_head head;
	struct rb_node rb_node;
};

static void pop_path_step(struct path *path, struct pos *order)
{
	struct path_step *step;
	if (!list_empty(&path->head)) {
		step = container_of(path->head.next, struct path_step, node);
		memcpy(order, &step->pos, sizeof(*order));
		memcpy(&path->u.s.start, &step->pos, sizeof(step->pos));
		list_move(&step->node, &free_path_steps);
	} 
}

static inline unsigned int get_distance_from_path(const struct path *path)
{
	struct path_step *step;
	step = list_entry(path->head.next, struct path_step, node);
	return step->distance;
}

static LIST_HEAD(free_paths);
static struct rb_root path_root = RB_ROOT;

static struct path *alloc_path(void)
{
	struct path *path;
	if (!list_empty(&free_paths)) {
		path = container_of(free_paths.next, struct path, head);
		list_del_init(&path->head);
	} else {
		path = malloc(sizeof(*path));
		if (!path)
			exit(-1);
	}
	rb_init_node(&path->rb_node);
	return path;
}

static struct path *copy_path(const struct path *source)
{
	struct path *path = alloc_path();
	struct path_step *source_step;
	struct path_step *step;

	if (!path) {
		exit(-1);
	}

	memcpy(path, source, sizeof(*path));
	INIT_LIST_HEAD(&path->head);
	rb_init_node(&path->rb_node);

	list_for_each_entry(source_step, &source->head, node) {
		step = alloc_path_step();
		memcpy(step, source_step, sizeof(*step));
		list_add_tail(&step->node, &path->head);
	}
	return path;
}

static void free_path(struct path *path)
{
	if (!path)
		return;
	list_splice_init(&path->head, &free_path_steps);
	list_del_init(&path->head);
	list_add_tail(&path->head, &free_paths);
}

static void add_path_to_heap(struct path *new)
{
	struct rb_node **link = &path_root.rb_node;
	struct rb_node *parent = NULL;
	struct path *path;
	const unsigned int value = new->u.value;
	
	while (*link) {
		parent = *link;
		path = rb_entry(parent, struct path, rb_node);

		if (path->u.value > value)
			link = &(*link)->rb_left;
		else
			link = &(*link)->rb_right;
	}

	rb_link_node(&new->rb_node, parent, link);
	rb_insert_color(&new->rb_node, &path_root);
}

static const struct path *find_path_in_heap(const struct pos *from, const struct pos *to)
{
	struct rb_node *node = path_root.rb_node;  /* top of the tree */
	unsigned int value;
	struct path temp;
	memcpy(&temp.u.s.start, from, sizeof(*from));
	memcpy(&temp.u.s.end, to, sizeof(*to));
	value = temp.u.value;

	while (node) {
		struct path *path = rb_entry(node, struct path, rb_node);

		if (path->u.value > value)
			node = node->rb_left;
		else if (path->u.value < value)
			node = node->rb_right;
		else
			return path;  /* Found it */
	}
	return NULL;
}

static inline bool same_pos(const struct pos *a, const struct pos *b)
{
	return (0 == memcmp(a, b, sizeof(*a)));
}

static void end_turn()
{
	fprintf(stdout, "go\n");
	fflush(stdout);
}

struct game_state {
	/* The following parameters are passed in during setup. */
	int32_t loadtime;
	int32_t turntime;
	int32_t rows;
	int32_t cols;
	int32_t turns;
	int32_t viewradius2;
	int32_t attackradius2;
	int32_t spawnradius2;
	int64_t player_seed;
	int32_t current_turn;
	unsigned int turn_start;
	unsigned int idle_ants;
	unsigned int max_ants;

	/* My resources */
	struct list_head my_hills;
	struct list_head ants_w_orders; 
	struct list_head ants_wo_orders;

	/* Targets */
	struct list_head food_wo_collectors;
	struct list_head food_w_collectors;
	struct list_head hills_wo_attackers;
	struct list_head hills_w_attackers;
	struct list_head enemy_ants_wo_attackers;
	struct list_head enemy_ants_w_attackers;
	struct list_head geographic_goals;

	struct list_head enemies;

	struct list_head free_food;
	struct list_head free_ants;
	struct list_head free_hills;
	struct list_head free_groups;
	struct list_head free_enemies;
	struct list_head free_geographic_goals;
	char *static_map;
	char *dynamic_map;
	char *ant_map;
	unsigned int *visited;

	struct list_head free_s_nodes;
	char *closed_set;
	FILE *log;
	FILE *input;
}; 

struct ant {
	struct list_head node;
	struct pos last_pos;
	struct pos pos;
	struct pos order;
	struct pos goal;
	enum DIRECTION dir;
	int owner;
	struct path *path;
};

static void get_next_move(struct game_state *state, struct ant *ant,
						  const struct pos *to, struct pos *order);

static bool can_be_seen(const struct game_state *state, const struct pos *pos);

static int16_t calc_distance(const struct game_state *state, int row1, int col1,
							 int row2, int col2)
{
	int abs1, abs2, dr, dc;

	abs1 = abs(row1 - row2);
	abs2 = state->rows - abs1;
	dr = (abs1 > abs2) ? abs2 : abs1;

	abs1 = abs(col1 - col2);
	abs2 = state->cols - abs1;
	dc = (abs1 > abs2) ? abs2 : abs1;
	
	return dr + dc;
}

static struct path *get_path_to(const struct game_state *state, const struct pos *from, const struct pos *goal);

/* Return the straight line distance between two locations on the map. */
static int16_t get_distance_to(const struct game_state *state, const struct pos *start,
			const struct pos *dest)
{
	int16_t distance;
	struct path *path = get_path_to(state, start, dest);
	if (path) {
		distance = get_distance_from_path(path);
		free_path(path);
	} else {
		return calc_distance(state, start->row, start->col, dest->row, dest->col);
	}
}

static int init_state(struct game_state *state)
{
	memset(state, 0, sizeof(*state));
	INIT_LIST_HEAD(&state->enemies);
	INIT_LIST_HEAD(&state->my_hills);
	INIT_LIST_HEAD(&state->ants_wo_orders);
	INIT_LIST_HEAD(&state->ants_w_orders);
	INIT_LIST_HEAD(&state->enemy_ants_wo_attackers);
	INIT_LIST_HEAD(&state->enemy_ants_w_attackers);
	INIT_LIST_HEAD(&state->hills_wo_attackers);
	INIT_LIST_HEAD(&state->hills_w_attackers);
	INIT_LIST_HEAD(&state->food_wo_collectors);
	INIT_LIST_HEAD(&state->food_w_collectors);
	INIT_LIST_HEAD(&state->geographic_goals);
	INIT_LIST_HEAD(&state->free_food);
	INIT_LIST_HEAD(&state->free_ants);
	INIT_LIST_HEAD(&state->free_hills);
	INIT_LIST_HEAD(&state->free_groups);
	INIT_LIST_HEAD(&state->free_enemies);
	INIT_LIST_HEAD(&state->free_s_nodes);
	INIT_LIST_HEAD(&state->free_geographic_goals);
}

static void assign_ant_to_goal(struct game_state *state, struct ant *ant, struct pos *goal)
{
	memcpy(&ant->goal, goal, sizeof(*goal));
	list_move_tail(&ant->node, &state->ants_w_orders);
	--state->idle_ants;
}

static char get_map(const struct game_state *state, const char *map, const struct pos *pos)
{
	return map[(pos->row * state->cols) + pos->col];
}

static bool is_water(const struct game_state *state, const struct pos *pos)
{
	return ('%' == get_map(state, state->static_map, pos));
}

static bool check_path_for_water(struct game_state *state, struct path *path)
{
	struct path_step *step;
	if (!path)
		return false;

	list_for_each_entry(step, &path->head, node) {
		if (is_water(state, &step->pos)) {
			return true;
		}
	}
	return false;
}

static void check_paths_for_water(struct game_state *state)
{
	struct ant *ant;
	list_for_each_entry(ant, &state->ants_wo_orders, node) {
		if (check_path_for_water(state, ant->path)) {
			free_path(ant->path);
			ant->path = NULL;
		}
	}
}

/* Goals that are met will have ant set. */
struct geographic_goal {
	struct list_head node;
	struct pos pos;
	struct ant *ant;
	enum {EXPLORE=0, HOLD} type;
};

static struct geographic_goal *alloc_goal(struct game_state *state)
{
	struct geographic_goal *goal;
	if (!list_empty(&state->free_geographic_goals)) {
		goal = container_of(state->free_geographic_goals.next, struct geographic_goal, node);
		list_del_init(&goal->node);
		goal->ant = NULL;
	} else {
		goal = malloc(sizeof(*goal));
		if (!goal)
			exit(-1);
		memset(goal, 0, sizeof(*goal));
		INIT_LIST_HEAD(&goal->node);
	}
	return goal;
}

static void free_goal(struct game_state *state, struct geographic_goal *goal)
{
	goal->ant = NULL;
	fprintf(state->log, "Freeing goal target (%d,%d) on turn %d\n", goal->pos.row,
			goal->pos.col, state->current_turn); 
	list_move(&goal->node, &state->free_geographic_goals);
}

static void free_goals(struct game_state *state, struct list_head *goals)
{
	list_splice_init(goals, &state->free_geographic_goals);
}

struct food {
	struct list_head node;
	struct pos pos;
	unsigned int last_seen;
};

static bool will_collide(const struct game_state *state, const struct pos *intended)
{
	struct ant *cur;
	struct food *food;

	if (is_water(state, intended)) {
		return true;
	}
	list_for_each_entry(food, &state->food_w_collectors, node) {
		if (same_pos(intended, &food->pos))
			return true;
	}
	list_for_each_entry(food, &state->food_wo_collectors, node) {
		if (same_pos(intended, &food->pos))
			return true;
	}
	list_for_each_entry(cur, &state->ants_w_orders, node) {
		if (same_pos(intended, &cur->order) || same_pos(intended, &cur->pos))
			return true;
	}
	list_for_each_entry(cur, &state->ants_wo_orders, node) {
		if (same_pos(intended, &cur->order) || same_pos(intended, &cur->pos))
			return true;
	}
	return false;
}

static void translate_pos(const struct game_state *state, const struct pos *pos,
						  const int x, const int y, struct pos *new_pos)
{
	int row;
	int col;
	row = (int)pos->row + x;
	if (row < 0)
		row = (int)state->rows + row;
	if (row >= (int)state->rows)
		row = row - (int)state->rows;
	new_pos->row = row;

	col = (int)pos->col + y;
	if (col < 0)
		col = (int)state->cols + col;
	if (col >= (int)state->cols)
		col = col - (int)state->cols;
	new_pos->col = col;
}

static void get_pos_from_direction(const struct game_state *state, const enum DIRECTION dir,
								   const struct pos *from, struct pos *to)
{
	switch (dir) {
		case N:
			translate_pos(state, from, -1, 0, to);
			break;
		case S:
			translate_pos(state, from, 1, 0, to);
			break;
		case E:
			translate_pos(state, from, 0, 1, to);
			break;
		case W:
			translate_pos(state, from, 0, -1, to);
			break;
		case MAX_DIRECTION:
			/* MAX_DIRECTION means to remain in the current position */
			memcpy(to, from, sizeof(*to));
			break;
		default:
			exit(-1);
	}
}

static enum DIRECTION get_dir_from_order(const struct game_state *state, const struct ant *ant,
										 const struct pos *order)
{
	int dir;
	struct pos new;
	for (dir = 0; dir < MAX_DIRECTION; ++dir) {
		get_pos_from_direction(state, dir, &ant->pos, &new);
		if (same_pos(order, &new))
			return dir;
	}
	return MAX_DIRECTION;
}

static bool order_ant(struct game_state *state, struct ant *ant, const struct pos *order)
{
	
	if (will_collide(state, order) || same_pos(&ant->pos, order)) {
		/* stay put for now */
		memcpy(&ant->order, &ant->pos, sizeof(ant->pos));
		return false;
	} else {
		static const char direction[MAX_DIRECTION] = {'N', 'E', 'S', 'W'};
		memcpy(&ant->order, order, sizeof(*order));
		fprintf(stdout, "o %d %d %c\n", ant->pos.row, ant->pos.col,
				direction[get_dir_from_order(state, ant, order)]);
		return true;
	}
}

struct hill {
	struct list_head node;
	struct list_head enemy_node;
	int owner;
	struct pos pos;
	unsigned int last_seen;
	unsigned int number_of_attackers;
};

struct enemy {
	int id;
	struct list_head node;
	int32_t number_of_ants;
	struct list_head ants;
	struct list_head hills;
};

int initialize_game_state(FILE *stream, struct game_state *state)
{
	int res = 0;
	char line[80];
	char command[80];
	int vals[3];
	int i;

	while (NULL != fgets(line, sizeof(line), stream)) {
		fputs(line, state->input);
		fflush(state->input);
		res = sscanf(line, "%s %d %d %d", command, &vals[0],
			     &vals[1], &vals[2]);
		if (res <= 0) {
			continue;
		}

		if (!strcasecmp(command, "loadtime")) {
			state->loadtime = vals[0];
		} else if (!strcasecmp(command, "turntime")) {
			state->turntime = vals[0];
		} else if (!strcasecmp(command, "turn")) {
			state->current_turn = vals[0];
		} else if (!strcasecmp(command, "rows")) {
			state->rows = vals[0];
		} else if (!strcasecmp(command, "cols")) {
			state->cols = vals[0];
		} else if (!strcasecmp(command, "turns")) {
			state->turns = vals[0];
		} else if (!strcasecmp(command, "viewradius2")) {
			state->viewradius2 = vals[0];
		} else if (!strcasecmp(command, "attackradius2")) {
			state->attackradius2 = vals[0];
		} else if (!strcasecmp(command, "spawnradius2")) {
			state->spawnradius2 = vals[0];
		} else if (!strcasecmp(command, "player_seed")) {
			state->player_seed = vals[0];
		} else if (!strcasecmp(command, "ready")) {
			break;
		} else {
			fprintf(state->log, "'%s' is unknown\n", command);
		}
	}

	srand(state->player_seed);

	state->static_map = malloc(state->rows * state->cols);
	state->dynamic_map = malloc(state->rows * state->cols);
	state->ant_map = malloc(state->rows * state->cols);
	state->visited = malloc(sizeof(*state->visited) * state->rows * state->cols);

	memset(state->static_map, '?', state->rows * state->cols);
	memset(state->dynamic_map, 0, state->rows * state->cols);
	memset(state->visited, 0, sizeof(*state->visited) * state->rows * state->cols);

	state->closed_set = malloc(sizeof(void *) * state->rows * state->cols);

	return 0;
}

static void fill_map_with_goals(struct game_state *state)
{
	int row, col;
	struct geographic_goal *goal;

	for (row = 0; row < state->rows; row += 8) {
		for (col = 0; col < state->cols; col += 8) {
			goal = alloc_goal(state);
			goal->pos.row = row;
			goal->pos.col = col;
			goal->type = EXPLORE;
			list_add_tail(&goal->node, &state->geographic_goals);
		}
	}
}

static void set_map(struct game_state *state, char *map, struct pos *pos, char val)
{
	map[(pos->row * state->cols) + pos->col] = val;
}

static struct food *get_free_food(struct game_state *state)
{
	struct food *food;
	if (!list_empty(&state->free_food)) {
		food = container_of(state->free_food.next, struct food, node);
		list_del_init(&food->node);
	} else {
		food = malloc(sizeof(*food));
		INIT_LIST_HEAD(&food->node);
	}
	return food;
}

static struct ant *get_free_ant(struct game_state *state)
{
	struct ant *ant;
	if (!list_empty(&state->free_ants)) {
		ant = container_of(state->free_ants.next, struct ant, node);
		list_del(&ant->node);
		free_path(ant->path);
	} else {
		ant = malloc(sizeof(*ant));
		memset(ant, 0, sizeof(*ant));
	}
	ant->dir = rand()&0x3;
	return ant;
}

struct enemy *get_enemy(struct game_state *state, const int id)
{
	struct enemy *enemy;

	/* First try to find one of the existing enemies. */
	list_for_each_entry(enemy, &state->enemies, node) {
		if (id == enemy->id) {
			return enemy;
		}
	}

	/* Must be a new enemy, allocate a new enemy structure. */
	enemy = malloc(sizeof(*enemy));
	memset(enemy, 0, sizeof(*enemy));
	enemy->id = id;
	INIT_LIST_HEAD(&enemy->ants);
	INIT_LIST_HEAD(&enemy->hills);
	list_add_tail(&enemy->node, &state->enemies);
	return enemy;
}

static void do_see_hill(struct game_state *state, int row, int col, int id)
{
	const struct pos pos = {.row = row, .col = col};
	struct enemy *enemy;
	struct hill *hill;

	/* Check if this hill is already known */
	if (id == 0) {
		list_for_each_entry(hill, &state->my_hills, node) {
			if (same_pos(&pos, &hill->pos)) {
				hill->last_seen = state->current_turn;
				return;
			}
		}
		hill = malloc(sizeof(*hill));
		hill->owner = 0;
		hill->last_seen = state->current_turn;
		memcpy(&hill->pos, &pos, sizeof(pos));
		list_add_tail(&hill->node, &state->my_hills);
		fprintf(state->log, "See my hill (%d,%d) on turn %d\n", hill->pos.row,
				hill->pos.col, state->current_turn); 
		return;
	} else {
		enemy = get_enemy(state, id);
		list_for_each_entry(hill, &enemy->hills, enemy_node) {
			if (same_pos(&hill->pos, &pos)) {
				/* This is a hill that we already know about. */
				hill->last_seen = state->current_turn;
				return;
			}
		}
		/* This is a new hill that we don't know about */
		hill = malloc(sizeof(*hill));
		hill->owner = id;
		hill->last_seen = state->current_turn;
		memcpy(&hill->pos, &pos, sizeof(pos));
		list_add_tail(&hill->node, &state->hills_wo_attackers);
		list_add_tail(&hill->enemy_node, &enemy->hills);
		fprintf(state->log, "See hill (%d,%d) on turn %d\n", hill->pos.row,
				hill->pos.col, state->current_turn); 
	}
	return;
}

void preturn_setup(struct game_state *state)
{
	struct enemy *enemy;
	struct ant *ant;
	struct hill *hill;

	state->idle_ants = 0;
	/* We're sent the list of all the food each turn so all the food on
	 * the active list can be moved to the free list. */
	list_splice_init(&state->food_w_collectors, &state->food_wo_collectors);
	
	list_splice_init(&state->hills_w_attackers, &state->hills_wo_attackers);
	list_for_each_entry(hill, &state->hills_wo_attackers, node) {
		hill->number_of_attackers = 0;
	}
	
	list_splice_init(&state->ants_w_orders, &state->ants_wo_orders);
}

static void do_see_ant(struct game_state *state, struct list_head *ants, const struct pos *const pos, int id)
{
	bool matched = false;
	struct ant *ant;

	list_for_each_entry(ant, ants, node) {
		if (same_pos(pos, &ant->order)) {
			memcpy(&ant->last_pos, &ant->pos, sizeof(*pos));
			memcpy(&ant->pos, pos, sizeof(*pos));
			list_move_tail(&ant->node, &state->ants_wo_orders);
			matched = true;
			++state->idle_ants;
			state->visited[pos->row * state->cols + pos->col]++;
			break;
		} 
	}
	if (!matched) {
		ant = get_free_ant(state);
		memcpy(&ant->pos, pos, sizeof(*pos));
		memcpy(&ant->last_pos, pos, sizeof(*pos));
		ant->owner = id;
		if (0 == ant->owner) {
			list_add_tail(&ant->node, &state->ants_wo_orders);
			++state->idle_ants;
			state->visited[pos->row * state->cols + pos->col]++;
		} else {
			list_add_tail(&ant->node, &state->enemy_ants_wo_attackers);
		}
	}
}

int read_turn_data(struct game_state *state, FILE *stream)
{
	int res;
	char line[80];
	char command[80];
	int vals[3];
	bool game_over = false;
	struct food *food, *food_n;
	struct ant *ant;
	LIST_HEAD(foods);
	LIST_HEAD(ants);

	int turn = -1;

	list_splice_init(&state->food_wo_collectors, &foods);
	list_splice_init(&state->ants_wo_orders, &ants);
	list_splice_init(&state->enemy_ants_w_attackers, &state->free_ants);
	list_splice_init(&state->enemy_ants_wo_attackers, &state->free_ants);

	while (NULL != fgets(line, sizeof(line), stream)) {
		fputs(line, state->input);
		fflush(state->input);
		fputs(line, state->log);
		res = sscanf(line, "%s %d %d %d", command, &vals[0],
			     &vals[1], &vals[2]);
		if (res <= 0) {
			continue;
		}

		if (!strcasecmp(command, "turn")) {
			turn = vals[0];
			state->current_turn = turn;
			state->turn_start = (unsigned long)clock();
		} else if (turn > -1) {
			if (!strcasecmp(command, "f")) {
				struct pos pos = { .row = vals[0], .col = vals[1]};
				bool matched = false;

				list_for_each_entry(food, &foods, node) {
					if (same_pos(&pos, &food->pos)) {
						food->last_seen = state->current_turn;
						list_move(&food->node, &state->food_wo_collectors);
						matched = true;
						break;
					}
				}

				if (!matched) {
					food = get_free_food(state);
					food->pos.row = vals[0];
					food->pos.col = vals[1];
					food->last_seen = state->current_turn;
					list_add_tail(&food->node, &state->food_wo_collectors);
				}
			} else if (!strcasecmp(command, "w")) {
				struct pos pos = { .row = vals[0], .col = vals[1]};
				set_map(state, state->static_map, &pos, '%');
			} else if (!strcasecmp(command, "a")) {
				struct pos pos = { .row = vals[0], .col = vals[1] };
				do_see_ant(state, &ants, &pos, vals[2]);
				if (1 == state->current_turn) {
					do_see_hill(state, vals[0], vals[1], 0);
				}
			} else if (!strcasecmp(command, "h")) {
				do_see_hill(state, vals[0], vals[1], vals[2]);
			} else if (!strcasecmp(command, "d")) {
				/* TODO: I do not currently care about the
				 * dead ants. */
			} else if (!strcasecmp(command, "go")) {
				break;
			} else if (!strcasecmp(command, "players")) {
			} else if (!strcasecmp(command, "score")) {
			} else {
				fprintf(state->log, "'%s' is unknown\n", command);
			}
		} else if (!strcasecmp(command, "end")) {
			game_over = true;
			turn = state->current_turn;
		} else {
			fprintf(state->log, "'%s' is unexpected here (%d)\n", command, __LINE__);
		}
	}

	list_for_each_entry_safe(food, food_n, &foods, node) {
		if (can_be_seen(state, &food->pos)) {
			/* Should have seen it */
			list_move(&food->node, &state->free_food);
		}
	}
	list_splice_init(&foods, &state->food_wo_collectors);
	list_splice_init(&ants, &state->free_ants);

	return (game_over || feof(stdin)) ? -1 : 0;
}

enum {RUNNING=0, OVER} game_state_t;

static enum DIRECTION get_direction_to(const struct game_state *state, const struct pos *from, const struct pos *to)
{
	struct pos pos;
	unsigned short int d;
	unsigned short int cur_distance = 10000;
	enum DIRECTION min_dir = rand()&0x3;
	int dir;

	for (dir = 0; dir < MAX_DIRECTION; ++dir) {
		get_pos_from_direction(state, dir, from, &pos);
		d = get_distance_to(state, &pos, to);
		if (cur_distance > d) {
			cur_distance = d;
			min_dir = dir;
		} else if ((cur_distance == d) && (rand()&0x1)) {
			cur_distance = d;
			min_dir = dir;
		}
	}

	return min_dir;
}

/* Returns a pointer to the ant cloest to pos that is currently idle */
static struct ant *find_nearest_idle_ant(const struct game_state *state, const struct pos *pos)
{
	struct ant *ant;
	struct ant *closest = NULL;
	int16_t d;
	int16_t new_d;

	list_for_each_entry(ant, &state->ants_wo_orders, node) {
		if (!closest) {
			closest = ant;
			d = get_distance_to(state, pos, &ant->pos);
			continue;
		}

		new_d = get_distance_to(state, pos, &ant->pos);
		if (d <= new_d)
			continue;

		closest = ant;
		d = new_d;
	}

	return closest;
}

/* Returns a pointer to the ant cloest to pos that is currently idle */
static struct ant *find_nearest_ant(const struct game_state *state, const struct pos *pos, const struct pos *skip)
{
	struct ant *ant;
	struct ant *closest = NULL;
	int16_t d;
	int16_t new_d;

	list_for_each_entry(ant, &state->ants_w_orders, node) {
		if (skip && same_pos(&ant->pos, skip))
			continue;

		if (!closest) {
			closest = ant;
			d = get_distance_to(state, pos, &ant->pos);
			continue;
		}

		new_d = get_distance_to(state, pos, &ant->pos);
		if (d <= new_d)
			continue;

		closest = ant;
		d = new_d;
	}
	list_for_each_entry(ant, &state->ants_wo_orders, node) {
		if (skip && same_pos(&ant->pos, skip))
			continue;

		if (!closest) {
			closest = ant;
			d = get_distance_to(state, pos, &ant->pos);
			continue;
		}

		new_d = get_distance_to(state, pos, &ant->pos);
		if (d <= new_d)
			continue;

		closest = ant;
		d = new_d;
	}

	return closest;
}

static bool can_be_seen(const struct game_state *state, const struct pos *pos)
{
	struct ant *ant;
	int i;
	ant = find_nearest_ant(state, pos, NULL);
	if (!ant)
		return false;
	i = calc_distance(state, pos->row, pos->col, ant->pos.row, ant->pos.col);
	i = get_distance_to(state, pos, &ant->pos);
	i *= i;
	return (i <= state->viewradius2);
}

static int distance_to_nearest_ant(const struct game_state *state, const struct pos *pos, const struct pos *skip)
{
	struct ant *ant = find_nearest_ant(state, pos, skip);
	if (!ant)
		return 1000;

	return get_distance_to(state, pos, &ant->pos);
}

static enum DIRECTION direction_farthest_out(const struct game_state *state, const struct ant *ant)
{
	int dir;
	struct pos new_pos;
	int ret = MAX_DIRECTION;
	int max_nearest = 0;
	int dist;

	for (dir = 0; dir < MAX_DIRECTION; ++dir) {
		get_pos_from_direction(state, dir, &ant->pos, &new_pos);
		if (is_water(state, &new_pos))
			continue;
		dist = distance_to_nearest_ant(state, &new_pos, &ant->pos);
		if (same_pos(&ant->last_pos, &new_pos))
			dist -= 20;
		if (dist < 0) dist = 0;
		if ((dist > max_nearest) || ((dist == max_nearest) && rand()&0x1)) {
			max_nearest = dist;
			ret = dir;
		}
	} 
	return ret;
}

static void direction_least_visited(const struct game_state *state, const struct ant *ant, struct pos *order)
{
	int dir;
	struct pos new_pos;
	int ret = MAX_DIRECTION;
	unsigned int least_visited = 0;
	unsigned int *offset;

	for (dir = 0; dir < MAX_DIRECTION; ++dir) {
		get_pos_from_direction(state, dir, &ant->pos, &new_pos);
		offset = &state->visited[new_pos.row*state->cols+new_pos.col];
		if (is_water(state, &new_pos))
			continue;
		if (*offset < least_visited) {
			least_visited = *offset;
			memcpy(order, &new_pos, sizeof(*order));
		} else if ((*offset == least_visited) && rand()&0x1) {
			least_visited = *offset;
			memcpy(order, &new_pos, sizeof(*order));
		}
	} 
	return;
}

static int distance_from_walls(struct game_state *state, struct pos *pos)
{
	struct pos cur;
	int weight = 0;

	for (cur.row = 0; cur.row < state->rows; ++cur.row) {
		for (cur.col = 0; cur.col < state->cols; ++cur.col) {
			if (is_water(state, &cur)) {
				weight += get_distance_to(state, &cur, pos);
			}
		}
	}
	return weight;
}

static int cmp_food(void *pvt, struct list_head *a, struct list_head *b)
{
	struct game_state *state = pvt;
	struct food *food_a = container_of(a, struct food, node);
	struct food *food_b = container_of(b, struct food, node);
	struct ant *ant_a = find_nearest_idle_ant(state, &food_a->pos);
	struct ant *ant_b = find_nearest_idle_ant(state, &food_b->pos);
	
	if (!ant_a || !ant_b)
		return 0;

	return (get_distance_to(state, &food_a->pos, &ant_a->pos) - 
		    get_distance_to(state, &food_b->pos, &ant_b->pos));
}

static int cmp_hill(void *pvt, struct list_head *a, struct list_head *b)
{
	struct game_state *state = pvt;
	struct hill *hill_a = container_of(a, struct hill, node);
	struct hill *hill_b = container_of(b, struct hill, node);
	struct ant *ant_a = find_nearest_idle_ant(state, &hill_a->pos);
	struct ant *ant_b = find_nearest_idle_ant(state, &hill_b->pos);
	
	if (!ant_a || !ant_b)
		return 0;

	return (get_distance_to(state, &hill_a->pos, &ant_a->pos) - 
		    get_distance_to(state, &hill_b->pos, &ant_b->pos));
}

static void remove_hold_goals_from_hill(struct game_state *state, const struct hill *hill);

static bool remove_destroyed_hills(struct game_state *state)
{
	struct hill *hill;
	struct hill *hill_n;
	bool my_hills_remain = false;

	/* Remove any hills that we can't see anymore */
	list_for_each_entry_safe(hill, hill_n, &state->hills_wo_attackers, node) {
		if (can_be_seen(state, &hill->pos) &&
			(hill->last_seen != state->current_turn)) {
			list_del_init(&hill->node);
			list_del_init(&hill->enemy_node);
			fprintf(state->log, "Can't see hill (%d,%d) on turn %d\n", hill->pos.row,
					hill->pos.col, state->current_turn); 
		}
	}
	list_for_each_entry_safe(hill, hill_n, &state->my_hills, node) {
		if (hill->last_seen != state->current_turn) {
			remove_hold_goals_from_hill(state, hill);
			list_del_init(&hill->node);
			fprintf(state->log, "Can't see my hill (%d,%d) on turn %d\n", hill->pos.row,
					hill->pos.col, state->current_turn); 
			continue;
		}
		my_hills_remain = true;
	}
	return my_hills_remain;
}

static struct geographic_goal *get_goal(struct game_state *state, const struct pos *pos)
{
	struct geographic_goal *goal;
	list_for_each_entry(goal, &state->geographic_goals, node) {
		if (same_pos(pos, &goal->pos))
			return goal;
	}
	return NULL;
}

static void add_hold_goals_to_hill(struct game_state *state, struct hill *hill)
{
	int i;
	struct pos corners[4];
	struct geographic_goal *goal;

	/* Completely surround the hill */
	translate_pos(state, &hill->pos, -1, -1, &corners[0]);
	translate_pos(state, &hill->pos, -1,  1, &corners[1]);
	translate_pos(state, &hill->pos,  1,  1, &corners[2]);
	translate_pos(state, &hill->pos,  1, -1, &corners[3]); 

	for (i = 0; i < ARRAY_SIZE(corners); ++i) {
		if (is_water(state, &corners[i]))
			continue;
		goal = get_goal(state, &corners[i]);
		if (goal)
			continue;

		goal = alloc_goal(state);
		goal->type = HOLD;
		memcpy(&goal->pos, &corners[i], sizeof(goal->pos));
		list_add_tail(&goal->node, &state->geographic_goals);
		fprintf(state->log, "Adding hold goal at (%d,%d) to (%d,%d) on turn %d\n",
				corners[i].row, corners[i].col, hill->pos.row, hill->pos.col,
				state->current_turn);
	}
}

static void remove_hold_goals_from_hill(struct game_state *state, const struct hill *hill)
{
	int i;
	struct pos corners[8];
	struct geographic_goal *goal;

	/* Completely surround the hill */
	translate_pos(state, &hill->pos, -1, -1, &corners[0]);
	translate_pos(state, &hill->pos, -1,  0, &corners[1]);
	translate_pos(state, &hill->pos, -1,  1, &corners[2]);
	translate_pos(state, &hill->pos,  0, -1, &corners[3]);
	translate_pos(state, &hill->pos,  0,  1, &corners[4]);
	translate_pos(state, &hill->pos,  1, -1, &corners[5]);
	translate_pos(state, &hill->pos,  1,  0, &corners[6]);
	translate_pos(state, &hill->pos,  1,  1, &corners[7]);

	for (i = 0; i < ARRAY_SIZE(corners); ++i) {
		goal = get_goal(state, &corners[i]);
		if (!goal)
			continue;
		free_goal(state, goal);
	}
}

static void add_hold_goals_to_all_hills(struct game_state *state)
{
	struct hill *hill;
	list_for_each_entry(hill, &state->my_hills, node)
		add_hold_goals_to_hill(state, hill);
}

static void add_exploration_goals_around_hill(struct game_state *state, struct hill *hill)
{
	int i;
	struct pos corners[8];
	struct geographic_goal *goal;

	/* Completely surround the hill */
	translate_pos(state, &hill->pos,-20, -20, &corners[0]);
	translate_pos(state, &hill->pos,  0, -20, &corners[1]);
	translate_pos(state, &hill->pos, 20, -20, &corners[2]);
	translate_pos(state, &hill->pos,-20,   0, &corners[3]);
	translate_pos(state, &hill->pos, 20,   0, &corners[4]);
	translate_pos(state, &hill->pos,-20,  20, &corners[5]);
	translate_pos(state, &hill->pos,  0,  20, &corners[6]);
	translate_pos(state, &hill->pos, 20,  20, &corners[7]);

	for (i = 0; i < ARRAY_SIZE(corners); ++i) {
		goal = get_goal(state, &corners[i]);
		if (goal)
			continue;
		goal = alloc_goal(state);
		memcpy(&goal->pos, &corners[i], sizeof(goal->pos));
		goal->type = EXPLORE;
		list_add_tail(&goal->node, &state->geographic_goals);
		fprintf(state->log, "Adding exploration target (%d,%d) on turn %d\n", goal->pos.row,
				goal->pos.col, state->current_turn); 
	}
}


static void add_exploration_goals_around_all_hills(struct game_state *state)
{
	struct hill *hill;
	list_for_each_entry(hill, &state->my_hills, node)
		add_exploration_goals_around_hill(state, hill);
}

static struct ant *get_ant(struct game_state *state, const struct pos *pos)
{
	struct ant *ant;
	list_for_each_entry(ant, &state->ants_w_orders, node) {
		if (same_pos(pos, &ant->pos))
			return ant;
	}
	list_for_each_entry(ant, &state->ants_wo_orders, node) {
		if (same_pos(pos, &ant->pos))
			return ant;
	}
	return NULL;
}

static bool can_enemy_see_hill(const struct game_state *state, const struct hill *hill)
{
	struct ant *ant;
	list_for_each_entry(ant, &state->enemy_ants_wo_attackers, node) {
		if (pow(get_distance_to(state, &hill->pos, &ant->pos), 2) < state->viewradius2)
			return true;
	}
	return false;
}

static void update_goals(struct game_state *state)
{
	struct geographic_goal *goal;
	struct geographic_goal *goal_n;
	struct hill *hill;
	int exploration_goal_count = 0;
	struct ant *ant;

	/*
	if (1 == state->current_turn) {
		add_exploration_goals_around_all_hills(state);
	}
	*/

	if ((state->max_ants > 40) && (state->idle_ants*2 < state->max_ants)) {
		add_hold_goals_to_all_hills(state);
	} else {
		list_for_each_entry(hill, &state->my_hills, node) {
			if (can_enemy_see_hill(state, hill)) {
				add_hold_goals_to_hill(state, hill);
			}
		}
	}

	/* Place a single goal on enemy hills. */
	list_for_each_entry(hill, &state->hills_wo_attackers, node) {
		goal = get_goal(state, &hill->pos);
		if (goal)
			continue;
		goal = alloc_goal(state);
		goal->type = EXPLORE;
		memcpy(&goal->pos, &hill->pos, sizeof(goal->pos));
		list_add(&goal->node, &state->geographic_goals);
	}

	/* First cleanup / update any achieved exploration goals */
	list_for_each_entry_safe(goal, goal_n, &state->geographic_goals, node) {
		goal->ant = get_ant(state, &goal->pos);

		if (goal->ant && (EXPLORE == goal->type)) {
			free_goal(state, goal);
			continue;
		}

		/* Goals cannot be on "water" tiles */
		if (is_water(state, &goal->pos)) {
			free_goal(state, goal);
			continue;
		}

		if (EXPLORE == goal->type)
			++exploration_goal_count;
	}
}

static int cmp_goals(void *pvt, struct list_head *a, struct list_head *b)
{
	struct game_state *state = pvt;
	struct geographic_goal *goal_a = container_of(a, struct geographic_goal, node);
	struct geographic_goal *goal_b = container_of(b, struct geographic_goal, node);
	struct ant *ant_a = find_nearest_idle_ant(state, &goal_a->pos);
	struct ant *ant_b = find_nearest_idle_ant(state, &goal_b->pos);
	
	if (!ant_a || !ant_b)
		return 0;

	return (get_distance_to(state, &goal_a->pos, &ant_a->pos) - 
		    get_distance_to(state, &goal_b->pos, &ant_b->pos));
}

static void assign_idle_ants_to_goals(struct game_state *state, bool explore)
{
	struct geographic_goal *goal;
	struct ant *ant;

	list_sort(state, &state->geographic_goals, cmp_goals);

	list_for_each_entry(goal, &state->geographic_goals, node) {
		if ((EXPLORE == goal->type) && !explore)
			continue;
		if ((EXPLORE != goal->type) && explore)
			continue;
		ant = find_nearest_idle_ant(state, &goal->pos);
		if (!ant)
			return;

		assign_ant_to_goal(state, ant, &goal->pos);
	}
}

static void assign_ants_to_enemy_hills(struct game_state *state)
{
	struct ant *ant;
	struct hill *hill;
	struct hill *hill_n;

	float number_of_ants_to_rush = 0.25 * (float)state->idle_ants;

	/* Do we have a line on any undefended hills? */
	if (!list_empty(&state->hills_wo_attackers)) {
		while (!list_empty(&state->ants_wo_orders)) {
			list_sort(state, &state->hills_wo_attackers, cmp_hill);
			list_for_each_entry_safe(hill, hill_n, &state->hills_wo_attackers, node) {
				ant = find_nearest_idle_ant(state, &hill->pos);
				if (!ant)
					return;
				assign_ant_to_goal(state, ant, &hill->pos);
				if (++hill->number_of_attackers > number_of_ants_to_rush)
					return;
			}
		}
	}
	return;
}

static void move_ants_towards_goals(struct game_state *state)
{
	struct ant *ant;
	struct pos order;

	/* Try to move all the ants towards their goals */
	list_for_each_entry(ant, &state->ants_w_orders, node) {
		get_next_move(state, ant, &ant->goal, &order);
		order_ant(state, ant, &order);
		if (((((unsigned long)clock() - state->turn_start)*1000) / CLOCKS_PER_SEC) > (state->turntime - 75)) {
			return;
		}
	}
}

static void assign_idle_ants_to_food(struct game_state *state)
{
	struct food *food;
	struct food *food_n;
	struct ant *ant;

	/* In order to make sure all ants are moving towards the food that it can
	 * collect in the least number of turn, sort the food list based on how
	 * far away each food is away from the collector. */
	list_sort(state, &state->food_wo_collectors, cmp_food);

	/* Assign new food goals first */
	list_for_each_entry_safe(food, food_n, &state->food_wo_collectors, node) {
		ant = find_nearest_idle_ant(state, &food->pos);
		if (!ant) {
			/* No more ants available to gather food */
			break;
		}

		assign_ant_to_goal(state, ant, &food->pos);
		list_move(&food->node, &state->food_w_collectors);
	}
}

static void spread_out_idle_ants(struct game_state *state)
{
	struct ant *ant;
	struct pos order;
	/* Spread the remaining ants out */
	list_for_each_entry(ant, &state->ants_wo_orders, node) {
		direction_least_visited(state, ant, &order);
		order_ant(state, ant, &order);
	}
}

int do_turn(struct game_state *state, FILE *stream)
{
	struct food *food;
	struct food *food_n;
	struct ant *ant;
	struct ant *ant_n;
	int res;
	int i;
	unsigned long start;

	preturn_setup(state);

	fprintf(state->log, "===== Starting turn %d =====\n", state->current_turn+1);
	res = read_turn_data(state, stream);
	if (res < 0) {
		return OVER;
	}

	if (state->idle_ants > state->max_ants)
		state->max_ants = state->idle_ants;

	/* Update the ant map with all ants so we can path around them. */
	memcpy(state->ant_map, state->static_map, state->rows * state->cols);
	list_for_each_entry(ant, &state->ants_wo_orders, node)
		set_map(state, state->ant_map, &ant->pos, '%');

	remove_destroyed_hills(state);
	update_goals(state);

	assign_idle_ants_to_goals(state, false);
	assign_idle_ants_to_food(state);
	assign_idle_ants_to_goals(state, true);
	assign_ants_to_enemy_hills(state);
	move_ants_towards_goals(state);
	spread_out_idle_ants(state);

	end_turn();
	return RUNNING;
}

void cleanup_game_state(struct game_state *state)
{
	int x;
	int y;
	struct ant *ant;
	struct enemy *enemy;
	struct hill *hill;
	struct food *food;
	LIST_HEAD(local);

	list_splice_init(&state->ants_wo_orders, &local);
	list_splice_init(&state->ants_w_orders, &local);
	list_splice_init(&state->enemy_ants_w_attackers, &local);
	list_splice_init(&state->enemy_ants_wo_attackers, &local);
	list_splice_init(&state->free_ants, &local);
	while (!list_empty(&local)) {
		ant = container_of(local.next, struct ant, node);
		list_del(&ant->node);
		free(ant);
	}

	list_splice_init(&state->food_wo_collectors, &local);
	list_splice_init(&state->food_w_collectors, &local);
	list_splice_init(&state->free_food, &local);
	while (!list_empty(&local)) {
		food = container_of(local.next, struct food, node);
		list_del(&food->node);
		free(food);
	}
	
	free(state->static_map);
	free(state->dynamic_map);
	free(state->ant_map);
	if (state->log != stderr) {
		fclose(state->log);
	}
}

struct search_node {
	/* For returning paths, and storing on the free list */
	struct list_head node;
	struct pos pos;
	struct rb_node rb_node;

	struct search_node *parent;

	unsigned int g;
	unsigned int h;
	unsigned int f;
};

static struct search_node *alloc_search_node(struct game_state *state, const struct pos *pos)
{
	struct search_node *s_node = NULL;

	if (!list_empty(&state->free_s_nodes)) {
		s_node = container_of(state->free_s_nodes.next, struct search_node, node);
		list_del_init(&s_node->node);
	} else {
		s_node = malloc(sizeof(*s_node));
		if (!s_node)
			exit(-1);
		memset(s_node, 0, sizeof(*s_node));
		INIT_LIST_HEAD(&s_node->node);
	}

	s_node->parent = NULL;
	memcpy(&s_node->pos, pos, sizeof(*pos));
	rb_init_node(&s_node->rb_node);
	s_node->g = s_node->h = s_node->h = 0;
	return s_node;
}

static inline bool is_node_equal(const struct search_node *a, const struct search_node *b)
{
	return (0 == memcmp(&a->pos, &b->pos, sizeof(a->pos)));
}

static inline void clear_closed_set(struct game_state *state)
{
	memset(state->closed_set, 0, state->rows * state->cols);
}

static inline bool in_closed_set(struct game_state *state, const struct pos *pos)
{
	return (state->closed_set[pos->row * state->cols + pos->col] == 1);
}

static inline bool in_open_set(struct game_state *state, const struct pos *pos)
{
	return (state->closed_set[pos->row * state->cols + pos->col] == 2);
}

static void add_to_tree(struct game_state *state, struct rb_root *root, struct search_node *new)
{
	struct rb_node **link = &root->rb_node, *parent;
	unsigned int value = new->f;
	
	while (*link) {
		parent = *link;
		struct search_node *node = rb_entry(parent, struct search_node, rb_node);

		if (node->f > value)
			link = &(*link)->rb_left;
		else
			link = &(*link)->rb_right;
	}

	rb_link_node(&new->rb_node, parent, link);
	rb_insert_color(&new->rb_node, root);
	state->closed_set[new->pos.row * state->cols + new->pos.col] = 2;
}

static struct search_node *pop_from_tree(struct game_state *state, struct rb_root *tree)
{
	struct search_node *ret;
	struct rb_node *n = rb_first(tree);
	if (n) {
		rb_erase(n, tree);
		ret = rb_entry(n, struct search_node, rb_node);
		state->closed_set[ret->pos.row * state->cols + ret->pos.col] = 0;
		return ret;
	}
	return NULL;
}

static int find_path_to(struct game_state *state, const struct pos *from,
						const struct pos *to, struct list_head *path)
{
	struct rb_root open_set = {0, };
	LIST_HEAD(closed_set_l);
	char *closed_set = state->closed_set;
	struct search_node *node;

	clear_closed_set(state);

	node = alloc_search_node(state, from);
	if (!node)
		exit(-1);

	node->g = 0;
	node->h = max(get_distance_to(state, from, to) - 3, 0);
	node->f = node->g + node->h;
	add_to_tree(state, &open_set, node);

	while ((node = pop_from_tree(state, &open_set))) {
		struct pos neighbors[MAX_DIRECTION];
		int dir;

		/* if node is goal */
		/*    make path */

		/* move node from the openset to the closedset */
		list_add(&node->node, &closed_set_l);
		closed_set[node->pos.row * state->cols + node->pos.col] = 1;

		for (dir = 0; dir < MAX_DIRECTION; ++dir) {
			struct search_node *n2;
			struct pos cur_pos;
			bool tentative_is_better;
			unsigned int tentative_g_score;

			get_pos_from_direction(state, dir, &node->pos, &cur_pos);
			if (in_closed_set(state, &cur_pos))
				continue;

			n2 = alloc_search_node(state, &cur_pos);
			n2->g = node->g + 1;
			n2->g = tentative_g_score;
			n2->h = get_distance_to(state, &n2->pos, to);
			n2->f = n2->g + n2->h;

			if (!in_open_set(state, &cur_pos)) {
				n2->parent = node;
				add_to_tree(state, &open_set, n2);
			} else if (tentative_g_score < n2->g) {
				n2->parent = node;
				tentative_is_better = true;
			} else {
				tentative_is_better = false;
			}
		}
	}

	list_splice_init(&closed_set_l, &state->free_s_nodes);
}

int main(int argc, char *argv[])
{
	int res;
	struct game_state game_state;
	bool game_over = false;
	FILE *stream = stdin;

	init_state(&game_state);
	game_state.input = fopen("/tmp/input.txt", "w");

	res = initialize_game_state(stream, &game_state);
	if (res < 0) {
		exit(res);
	}
	fill_map_with_goals(&game_state);

	game_state.log = fopen("/tmp/output.txt", "w");

	end_turn();

	while (RUNNING == do_turn(&game_state, stream)) {
		;
	}

	cleanup_game_state(&game_state);
	return 0;
}

struct square {
	struct list_head node;
	struct square *parent;
	struct pos pos;
	int f;
	int g;
};

static LIST_HEAD(free_squares);

static struct square *alloc_square()
{
	struct square *square;

	if (!list_empty(&free_squares)) {
		square = container_of(free_squares.next, struct square, node);
		list_del_init(&square->node);
	} else {
		square = malloc(sizeof(*square));
		if (!square)
			exit(1);
		memset(square, 0, sizeof(*square));
		INIT_LIST_HEAD(&square->node);
	}
	return square;
}

void neighbors(struct game_state *state, struct square *parent, struct list_head *adj)
{
	unsigned char row, col;
	enum DIRECTION d;

	for (d=0; d< MAX_DIRECTION; d++) {
		row = parent->pos.row;
		col = parent->pos.col;
		switch(d) {
		case N:
			if (row == 0)
				row = state->rows-1;
			else
				row = row-1;
			break;
		case E:
			if (col == state->cols-1)
				col = 0;
			else
				col = col+1;
			break;
		case S:
			if (row == state->rows-1)
				row = 0;
			else
				row = row+1;
			break;
		case W:
			if (col == 0)
				col = state->cols-1;
			else
				col = col-1;
			break;
		default:
			exit(1);
		}
		if (state->dynamic_map[row*state->cols+col] == '?') {
			struct square *square = alloc_square();
			if (!square)
				exit(-1);
			square->pos.row = row;
			square->pos.col = col;
			square->g = parent->g + 1;
			square->parent = parent;
			list_add_tail(&square->node, adj);
			state->dynamic_map[row*state->cols+col] = '2';
		} 
	}
	return;
}

static char min(char a, char b) {
	return (a < b) ? a : b;
}

/* Calculates the manhattan distance from start to goal
 * Stores it in start->f */
static void fu(struct game_state *state, struct square *start, struct square *goal) {
	char diff;
	diff = abs(start->pos.row - goal->pos.row);
	start->f = min(diff, state->rows-diff);
	diff = abs(start->pos.col - goal->pos.col);
	start->f += min(diff, state->cols-diff);
	start->f += start->g;
}

int print_dynamic_map(const struct game_state *state, FILE *stream)
{
	int x, y;

	for (x = 0; x < state->rows; ++x) {
		for (y = 0; y < state->cols; ++y) {
			fprintf(stream, "%c", state->dynamic_map[(x*state->cols)+y]);
		}
		fprintf(stream, "\n");
	}
}

static struct path *_astar(struct game_state *state, struct square *start, struct square *target)
{
	struct path *path = NULL;
	struct path_step *step;
	LIST_HEAD(open);
	LIST_HEAD(neigh);
	LIST_HEAD(closed);
	struct square *square, *lowest, *f;

	memcpy(state->dynamic_map, state->static_map, state->rows * state->cols);

	start->parent = NULL;
	start->g = 0;
	fu(state, start, target);
	list_add(&start->node, &open);
	while (!list_empty(&open)) {
		lowest = NULL;
		list_for_each_entry(square, &open, node) {
			// Find lowest F value
			if ((!lowest) || (lowest->f > square->f))
				lowest = square;
		}

		// Move current square from open to closed
		state->dynamic_map[lowest->pos.row*state->cols+lowest->pos.col] = '1';

		if (same_pos(&lowest->pos, &target->pos)) {
			unsigned int distance = 0;
			path = alloc_path();
			INIT_LIST_HEAD(&path->head);
			memcpy(&path->u.s.start, &start->pos, sizeof(start->pos));
			memcpy(&path->u.s.end, &target->pos, sizeof(target->pos));
			while (lowest->parent != NULL) {
				step = alloc_path_step();
				memcpy(&step->pos, &lowest->pos, sizeof(step->pos));
				step->distance = distance++;
				list_add(&step->node, &path->head);
				lowest = lowest->parent;
			};
			step = alloc_path_step();
			memcpy(&step->pos, &lowest->pos, sizeof(step->pos));
			step->distance = distance++;
			list_add(&step->node, &path->head);
			goto out;
		}

		// Add all valid neighbor moves onto the open list
		INIT_LIST_HEAD(&neigh);
		neighbors(state, lowest, &neigh);
		list_for_each_entry(f, &neigh, node) {
			fu(state, f, target);
		}
		list_splice_init(&neigh, &open);
		list_move(&lowest->node, &closed);
	}
out:
	list_splice_init(&closed, &free_squares);
	list_splice_init(&open, &free_squares);
	list_splice_init(&neigh, &free_squares);
	list_add_tail(&target->node, &free_squares);
	return path;
}

static struct path *get_path_to(const struct game_state *state, const struct pos *from, const struct pos *goal)
{
	const struct path *path;
	struct square *start;
	struct square *target;

	if (same_pos(from, goal)) {
		return NULL;
	}

	path = find_path_in_heap(from, goal);
	if (path) {
		return copy_path(path);
	}

	start = alloc_square();
	target = alloc_square();
	memcpy(&start->pos, from, sizeof(*from));
	memcpy(&target->pos, goal, sizeof(*goal));
	path = _astar((struct game_state *)state, start, target);
	if (!path) {
		return NULL;
	}
	add_path_to_heap(copy_path(path));
	return (struct path *)path;
}

static void get_next_move(struct game_state *state, struct ant *ant,
						  const struct pos *goal, struct pos *order)
{
	struct path_step *step;
	struct path *path = ant->path;
	if (same_pos(&ant->pos, goal)) {
		memcpy(order, &ant->pos, sizeof(*order));
		return;
	}
	if (path) {
		if (same_pos(&ant->pos, &path->u.s.start) && same_pos(goal, &path->u.s.end)) {
			pop_path_step(ant->path, order);
			return;
		} else {
			/* The path we have attached isn't to our current goal. Just scrap
			 * it. */
			free_path(path);
			ant->path = NULL;
		}
	}

	ant->path = get_path_to(state, &ant->pos, goal);
	/* Remove the start position from the path */
	pop_path_step(ant->path, order);
	/* Now get and return the next move. */
	pop_path_step(ant->path, order);
	return;
}

