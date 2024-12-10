/* boilerplate for this was taken from one of the imgui examples */
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h>
#include <math.h>

// [Win32] Our example includes a copy of glfw3.lib pre-compiled with VS2010 to maximize ease of testing and compatibility with old VS compilers.
// To link with VS2010-era libraries, VS2015+ requires linking with legacy_stdio_definitions.lib, which we do using this pragma.
// Your own project should not be affected, as you are likely to link with a newer binary of GLFW that is adequate for your version of Visual Studio.
#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

static void glfw_error_callback(int error, const char* description)
{
	fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

#define CELL_LOOKUP_BITS (4)
#define CELL_LOOKUP_NB   (1u << CELL_LOOKUP_BITS)
#define CELL_LOOKUP_MASK (CELL_LOOKUP_NB - 1u)

#define PAGE_XY_BITS    (4)
#define PAGE_XY_NB      (1u << PAGE_XY_BITS)
#define PAGE_X_MASK     (PAGE_XY_NB - 1)
#define PAGE_INDEX_MASK (PAGE_XY_NB*PAGE_XY_NB - 1)

#define EDGE_TYPE_NUM         (6)
#define EDGE_TYPE_NOTHING     (0)
#define EDGE_TYPE_SENDER      (1)
#define EDGE_TYPE_RECEIVER_F  (2)
#define EDGE_TYPE_RECEIVER_I  (3)
#define EDGE_TYPE_RECEIVER_DF (4)
#define EDGE_TYPE_RECEIVER_DI (5)

struct gridaddr {
	/* two's complement storage 0x80000000, ...., 0xfffffffe, 0xffffffff, 0x0, 0x1, 0x2, ...., 0x7fffffff */
	uint32_t y;
	uint32_t x;
};

static int gridaddr_add_check(struct gridaddr *p_dest, const struct gridaddr *p_src1, const struct gridaddr *p_src2) {
	uint32_t ix = p_src1->x;
	uint32_t iy = p_src1->y;
	uint32_t dx = p_src2->x;
	uint32_t dy = p_src2->y;
	uint32_t ox = ix + dx;
	uint32_t oy = iy + dy;
	p_dest->x = ox;
	p_dest->y = oy;
	return (int)(int32_t)((((ox ^ ix) & (ox ^ dx)) | ((oy ^ iy) & (oy ^ dy))) >> 31);
}

#define EDGE_DIR_N   (0)
#define EDGE_DIR_NE  (1)
#define EDGE_DIR_SE  (2)
#define EDGE_DIR_NW  (3)
#define EDGE_DIR_SW  (4)
#define EDGE_DIR_S   (5)
#define EDGE_DIR_NUM (6)
static int edge_dir_get_opposing(int dir) {
	assert(dir < EDGE_DIR_NUM);
	return 5 - dir;
}

#define VERTEX_DIR_E   (0)
#define VERTEX_DIR_NE  (1)
#define VERTEX_DIR_SE  (2)
#define VERTEX_DIR_NW  (3)
#define VERTEX_DIR_SW  (4)
#define VERTEX_DIR_W   (5)
#define VERTEX_DIR_NUM (6)
static int vertex_dir_get_opposing(int dir) {
	assert(dir < VERTEX_DIR_NUM);
	return 5 - dir;
}

/* How the grid is layed out:
 *
 *           x=0       x=1       x=2       x=3
 *        ___       ___       ___       ___       ___
 *       /   \     /   \     /   \     /   \     /   \     /
 * y=6  / 0,6 \___/ 1,6 \___/ 2,6 \___/ 3,6 \___/ 4,6 \___/ ...
 *      \     /   \     /   \     /   \     /   \     /
 * y=5   \___/ 0,5 \___/ 1,5 \___/ 2,5 \___/ 3,5 \___/ ...
 *       /   \     /   \     /   \     /   \     /
 * y=4  / 0,4 \___/ 1,4 \___/ 2,4 \___/ 3,4 \___/ ...
 *      \     /   \     /   \     /   \     /
 * y=3   \___/ 0,3 \___/ 1,3 \___/ 2,3 \___/ ...
 *       /   \     /   \     /   \     /
 * y=2  / 0,2 \___/ 1,2 \___/ 1,2 \___/ ...
 *      \     /   \     /   \     /
 * y=1   \___/ 0,1 \___/ 1,1 \___/ ...
 *       /   \     /   \     /             NW   N   NE
 * y=0  / 0,0 \___/ 1,0 \___/ ...            \  |  /
 *      \     /   \     /                  W __\|/__ E
 * y=-1  \___/ 0,-1\___/ ...                   /|\
 *       /   \     /                         /  |  \
 * y=-2 / 0,-2\___/ ...                    SW   S   SE
 *      \     /
 *       \___/
 *      
 * For edges             For vertices
 * ---------             ------------
 *
 * Common rules:
 *  N  = (x,   y+2)      E  = (x+1, y)
 *  S  = (x,   y-2)      W  = (x-1, y)
 * 
 * If y is even:
 *  NE = (x,   y+1)      NE = (x,   y+3)
 *  SE = (x,   y-1)      SE = (x,   y-3)
 *  SW = (x-1, y-1)      SW = (x-1, y-3)
 *  NW = (x-1, y+1)      NW = (x-1, y+3)
 * 
 * If y is odd:
 *  NE = (x+1, y+1)      NE = (x+1, y+3)
 *  SE = (x+1, y-1)      SE = (x+1, y-3)
 *  SW = (x,   y-1)      SW = (x,   y-3)
 *  NW = (x,   y+1)      NW = (x,   y+3)
 */

static int gridaddr_edge_neighbour(struct gridaddr *p_dest, const struct gridaddr *p_src, int edge_direction) {
	static const uint32_t y_offsets[6] =
		{   2   /* N */
		,   1   /* NE */
		,  -1   /* SE */
		,   1   /* NW */
		,  -1   /* SW */
		,  -2   /* S */
		};
	static const uint32_t x_offsets[6][2] =
		/*  y_even  y_odd */
		{   {0,     0} /* N */
		,   {0,     1} /* NE */
		,   {0,     1} /* SE */
		,   {-1,    0} /* NW */
		,   {-1,    0} /* SW */
		,   {0,     0} /* S */
		};
	struct gridaddr offset;
	assert(edge_direction < EDGE_DIR_NUM);
	offset.x = x_offsets[edge_direction][p_src->y & 1];
	offset.y = y_offsets[edge_direction];
	return gridaddr_add_check(p_dest, p_src, &offset);
}

static int gridaddr_vertex_neighbour(struct gridaddr *p_dest, const struct gridaddr *p_src, int virtex_direction) {
	static const uint32_t y_offsets[6] =
		{   0   /* E */
		,   3   /* NE */
		,  -3   /* SE */
		,   3   /* NW */
		,  -3   /* SW */
		,   0   /* W */
		};
	static const uint32_t x_offsets[6][2] =
		/*  y_even  y_odd */
		{   {1,     1} /* E */
		,   {0,     1} /* NE */
		,   {0,     1} /* SE */
		,   {-1,    0} /* NW */
		,   {-1,    0} /* SW */
		,   {-1,   -1} /* W */
		};
	struct gridaddr offset;
	assert(virtex_direction < VERTEX_DIR_NUM);
	offset.x = x_offsets[virtex_direction][p_src->y & 1];
	offset.y = y_offsets[virtex_direction];
	return gridaddr_add_check(p_dest, p_src, &offset);
}


#define REGISTER_FILE_BITS (16)

/*
 0000 z  - zero the bit state
 0001 l  - load the bit state from the IO register file
 0002 o  - bitwise OR the bit at the address into the state
 0003 i  - bitwise OR the inverse of the bit at the address into the state
 0004 od - bitwise OR the previous bit at the address into the state
 0005 id - bitwise OR the inverse of the previous bit at the address into the state
 0006 w  - write bit state to the given address
 0007 s  - store bit state to the IO register file

*/

struct gridcell {
	/* LSB
	 * 0..7  - is the index of the cell within the page (2*PAGE_XY_BITS bits)

	 * 8:10  - N_EDGE
	 * 11:13 - NE_EDGE
	 * 14:16 - SE_EDGE
	 * 17:19 - NW_EDGE
	 * 20:22 - SW_EDGE
	 * 23:25 - S_EDGE
	 * 
	 * 26    - E_VERTEX
	 * 27    - NE_VERTEX
	 * 28    - SE_VERTEX
	 * 29    - NW_VERTEX
	 * 30    - SW_VERTEX
	 * 31    - W_VERTEX

	 * 32    - CURRENT_VALUE
	 * 33    - PREVIOUS_VALUE
	 * 34    - EXTERNAL_SUPPLY
	 * 35    - IS_COMPUTING_VALUE
	 * 36    - VALUE_IS_COMPUTED
	 * 37    - VISITED_TEMP */


	uint64_t         flags;

};

#define CELLFLAG_CURRENT_VALUE_BIT    (32)
#define CELLFLAG_CURRENT_VALUE_MASK   (1ull << CELLFLAG_CURRENT_VALUE_BIT)
#define CELLFLAG_DELAY_VALUE_BIT      (33)
#define CELLFLAG_DELAY_VALUE_MASK     (1ull << CELLFLAG_DELAY_VALUE_BIT)
#define CELLFLAG_EXTERNAL_SUPPLY_BIT  (34)
#define CELLFLAG_EXTERNAL_SUPPLY_MASK (1ull << CELLFLAG_EXTERNAL_SUPPLY_BIT)
#define CELLFLAG_IS_COMPUTING_MASK    (1ull << 35)
#define CELLFLAG_IS_COMPUTED_MASK     (1ull << 36)

static void rt_assert_impl(int condition, const char *p_cond_str, const char *p_file, const int line) {
	if (!condition) {
		fprintf(stderr, "assertion failure(%s:%d): %s\n", p_file, line, p_cond_str);
		abort();
	}
}
#define RT_ASSERT(condition_) rt_assert_impl((condition_), #condition_, __FILE__, __LINE__)

#ifndef NDEBUG
#define DEBUG_EVAL(x_) ((void)(x_))
#else
#define DEBUG_EVAL(x_) ((void)0)
#endif


struct gridpage {
	struct gridcell   data[PAGE_XY_NB*PAGE_XY_NB];
	struct gridstate *p_owner;
	struct gridpage  *lookups[CELL_LOOKUP_NB];
	struct gridaddr   position; /* position of data[0] in the full grid  */
	

};

struct solve_stats {
	/* Grid dimensions */
	int32_t min_x;
	int32_t max_x;
	int32_t min_y;
	int32_t max_y;

	int32_t num_edgeops;
	int32_t num_vertops;
	int32_t num_cells;

};

struct gridstate {
	struct gridpage    *p_root;
	int                 stats_ok;
	struct solve_stats  stats;

};

void gridstate_init(struct gridstate *p_gridstate) {
	p_gridstate->p_root = NULL;
	p_gridstate->stats_ok = 0;
}

static uint64_t xy_to_id(uint32_t x, uint32_t y) {
	uint64_t grp = (((uint64_t)y) << 32) | x;
	uint64_t umix = grp * 8249772677985670961ull;
	return (umix >> 48) ^ umix;
}

static void id_to_xy(uint64_t id, uint32_t *p_x, uint32_t *p_y) {
	uint64_t umix = (id >> 48) ^ id;
	uint64_t grp = umix * 7426732773883044305ull;
	*p_x = (uint32_t)grp;
	*p_y = (uint32_t)(grp >> 32);
}

static void verify_gridpage(struct gridpage *p_page) {
	int i;
	RT_ASSERT(p_page->p_owner != NULL);
	RT_ASSERT((p_page->position.x & PAGE_X_MASK) == 0);
	RT_ASSERT((p_page->position.y & PAGE_X_MASK) == 0);
	for (i = 0; i < PAGE_XY_NB*PAGE_XY_NB; i++)
		RT_ASSERT((p_page->data[i].flags & PAGE_INDEX_MASK) == i);
}

static struct gridpage *gridstate_get_gridpage(struct gridstate *p_grid, const struct gridaddr *p_page_addr, int permit_create) {
	struct gridpage **pp_c = &(p_grid->p_root);
	struct gridpage *p_c   = *pp_c;
	uint32_t page_x        = p_page_addr->x;
	uint32_t page_y        = p_page_addr->y;
	uint64_t page_id;
	int      i;

	assert((page_x & PAGE_X_MASK) == 0);
	assert((page_y & PAGE_X_MASK) == 0);

	page_id = xy_to_id(page_x, page_y);

	while (p_c != NULL) {
		if (p_c->position.x == page_x && p_c->position.y == page_y)
			return p_c;
		pp_c    = &(p_c->lookups[page_id & CELL_LOOKUP_MASK]);
		p_c     = *pp_c;
		page_id = page_id >> CELL_LOOKUP_BITS;
	}

	if ((!permit_create) || (p_c = (struct gridpage *)malloc(sizeof(*p_c))) == NULL)
		return NULL;

	p_c->p_owner    = p_grid;
	p_c->position.x = page_x;
	p_c->position.y = page_y;
	for (i = 0; i < CELL_LOOKUP_NB; i++)
		p_c->lookups[i] = NULL;
	for (i = 0; i < PAGE_XY_NB * PAGE_XY_NB; i++)
		p_c->data[i].flags = i;

	DEBUG_EVAL(verify_gridpage(p_c));

	*pp_c = p_c;
	return p_c;
}

static uint32_t gridaddr_split(struct gridaddr *p_page_addr, const struct gridaddr *p_addr) {
	p_page_addr->x = p_addr->x & ~(uint32_t)PAGE_X_MASK;
	p_page_addr->y = p_addr->y & ~(uint32_t)PAGE_X_MASK;
	return (p_addr->x & PAGE_X_MASK) | ((p_addr->y & PAGE_X_MASK) << PAGE_XY_BITS);
}

static struct gridcell *gridstate_get_gridcell(struct gridstate *p_grid, const struct gridaddr *p_addr, int permit_create) {
	struct gridaddr page_addr;
	uint32_t cell_index = gridaddr_split(&page_addr, p_addr);
	struct gridpage *p_page = gridstate_get_gridpage(p_grid, &page_addr, permit_create);
	if (p_page == NULL)
		return NULL;
	return &(p_page->data[cell_index]);
}

/* Given a cell, find the page which it is part of */
static struct gridpage *gridcell_get_gridpage_and_full_addr(struct gridcell *p_cell, struct gridaddr *p_addr) {
	uint32_t         page_index = p_cell->flags & PAGE_INDEX_MASK;
	struct gridpage *p_page = (struct gridpage *)(p_cell - page_index);
	DEBUG_EVAL(verify_gridpage(p_page));
	p_addr->x = p_page->position.x | (page_index & PAGE_X_MASK);
	p_addr->y = p_page->position.y | (page_index >> PAGE_XY_BITS);
	return p_page;
}

static struct gridpage *gridpage_get_gridpage(struct gridpage *p_page, const struct gridaddr *p_page_addr, int permit_create) {
	/* This is a fast path for when the page address is the supplied page. */
	if (p_page->position.x == p_page_addr->x && p_page->position.y == p_page_addr->y)
		return p_page;
	assert(p_page->p_owner != NULL);
	return gridstate_get_gridpage(p_page->p_owner, p_page_addr, permit_create);
}

static struct gridcell *gridpage_get_gridcell(struct gridpage *p_page, const struct gridaddr *p_addr, int permit_create) {
	struct gridaddr page_addr;
	uint32_t        page_index = gridaddr_split(&page_addr, p_addr);
	if ((p_page = gridpage_get_gridpage(p_page, &page_addr, permit_create)) == NULL)
		return NULL;
	return &(p_page->data[page_index]);
}

static void gridcell_validate_edge_relationship(struct gridcell *p_local, struct gridcell *p_neighbour, int local_to_neighbour_direction) {
	int neighbour_to_local_direction = edge_dir_get_opposing(local_to_neighbour_direction);
	int neighbour_edge_control       = (p_neighbour->flags >> (8 + neighbour_to_local_direction * 3)) & 0x7;
	int local_edge_control           = (p_local->flags >> (8 + local_to_neighbour_direction * 3)) & 0x7;
	RT_ASSERT(neighbour_edge_control < EDGE_TYPE_NUM);
	RT_ASSERT(local_edge_control < EDGE_TYPE_NUM);
	if (local_edge_control == EDGE_TYPE_SENDER) {
		RT_ASSERT(neighbour_edge_control != EDGE_TYPE_NOTHING);
		RT_ASSERT(neighbour_edge_control != EDGE_TYPE_SENDER);
	} else if (local_edge_control == EDGE_TYPE_NOTHING) {
		RT_ASSERT(neighbour_edge_control == EDGE_TYPE_NOTHING);
	} else {
		RT_ASSERT(neighbour_edge_control == EDGE_TYPE_SENDER);
	}
}

static struct gridcell *gridcell_get_edge_neighbour(struct gridcell *p_cell, int direction, int permit_create) {
	struct gridaddr  neighbour_addr;
	struct gridpage *p_cell_page = gridcell_get_gridpage_and_full_addr(p_cell, &neighbour_addr);

	assert(direction < EDGE_DIR_NUM); /* there are no east and west edges in the hex grid, just as there are no north and south vertices */
	
	if (gridaddr_edge_neighbour(&neighbour_addr, &neighbour_addr, direction))
		return NULL;

	return gridpage_get_gridcell(p_cell_page, &neighbour_addr, permit_create);
}

static struct gridcell *gridcell_get_vertex_neighbour(struct gridcell *p_cell, int direction, int permit_create) {
	struct gridaddr  neighbour_addr;
	struct gridpage *p_cell_page = gridcell_get_gridpage_and_full_addr(p_cell, &neighbour_addr);

	assert(direction < VERTEX_DIR_NUM); /* there are no east and west edges in the hex grid, just as there are no north and south vertices */
	
	if (gridaddr_vertex_neighbour(&neighbour_addr, &neighbour_addr, direction))
		return NULL;

	return gridpage_get_gridcell(p_cell_page, &neighbour_addr, permit_create);
}

/* Returns the old flags */
static void set_flags(uint64_t *p_flags, int mask, int position, int new_flags) {
	uint64_t flags = *p_flags;
	uint64_t umask = ((uint64_t)mask) << position;
	uint64_t newv  = ((uint64_t)new_flags) << position;
	*p_flags = (flags & ~umask) | newv;
}

#if 0
static int update_flags(uint64_t *p_flags, int mask, int position, int new_flags) {
	uint64_t flags = *p_flags;
	uint64_t umask = ((uint64_t)mask) << position;
	uint64_t oldv  = flags & umask;
	uint64_t newv  = ((uint64_t)new_flags) << position;
	assert((newv & ~umask) == 0);
	*p_flags = (flags & ~oldv) | newv;
	return (int)(oldv >> position);
}
#endif

static int gridcell_get_edge_flags(const struct gridcell *p_cell, int direction) {
	assert(direction < EDGE_DIR_NUM);
	return (p_cell->flags >> (8 + direction * 3)) & 0x7;
}

static void gridcell_set_edge_flags_adv(struct gridcell *p_cell, struct gridcell *p_neighbour, int direction, int control) {
	assert(direction < EDGE_DIR_NUM);
	assert(control != EDGE_TYPE_SENDER && control < EDGE_TYPE_NUM);
	set_flags(&(p_cell->flags),      0x7, 8+direction*3,                        control);
	set_flags(&(p_neighbour->flags), 0x7, 8+edge_dir_get_opposing(direction)*3, (control == EDGE_TYPE_NOTHING) ? EDGE_TYPE_NOTHING : EDGE_TYPE_SENDER);
	DEBUG_EVAL(gridcell_validate_edge_relationship(p_cell, p_neighbour, direction));
}

static int gridcell_set_edge_flags(struct gridcell *p_cell, int direction, int control) {
	struct gridcell *p_neighbour;
	assert(control != EDGE_TYPE_SENDER && control < EDGE_TYPE_NUM);
	if ((p_neighbour = gridcell_get_edge_neighbour(p_cell, direction, 1)) == NULL)
		return 1;
	gridcell_set_edge_flags_adv(p_cell, p_neighbour, direction, control);
	return 0;
}

static int gridcell_get_vert_flags(const struct gridcell *p_cell, int direction) {
	assert(direction < VERTEX_DIR_NUM);
	return (p_cell->flags >> (26 + direction)) & 0x1;
}

static void gridcell_set_vert_flags_adv(struct gridcell *p_cell, struct gridcell *p_neighbour, int direction, int is_linked) {
	assert(is_linked == 0 || is_linked == 1);
	assert(direction < VERTEX_DIR_NUM);
	set_flags(&(p_cell->flags),      0x1, 26+direction,                          is_linked);
	set_flags(&(p_neighbour->flags), 0x1, 26+vertex_dir_get_opposing(direction), is_linked);
}

static int gridcell_set_vert_flags(struct gridcell *p_cell, int direction, int is_linked) {
	struct gridcell *p_neighbour;
	if ((p_neighbour = gridcell_get_vertex_neighbour(p_cell, direction, 1)) == NULL)
		return 1;
	gridcell_set_vert_flags_adv(p_cell, p_neighbour, direction, is_linked);
	return 0;
}

static void mark_net_as_computing_mask(struct gridcell *p_cell) {
	if ((p_cell->flags & CELLFLAG_IS_COMPUTING_MASK) == 0) {
		unsigned i;
		p_cell->flags |= CELLFLAG_IS_COMPUTING_MASK;
		for (i = 0; i < VERTEX_DIR_NUM; i++) {
			if ((p_cell->flags >> (26 + i)) & 1) {
				mark_net_as_computing_mask(gridcell_get_vertex_neighbour(p_cell, i, 0));
			}
		}
	}
}


static int grid_cell_solve(struct gridcell *p_cell, struct solve_stats *p_stats);

static int solve_net_recursive(struct gridcell *p_cell, uint32_t *p_value, struct solve_stats *p_stats) {
	unsigned i;
	uint32_t value = p_cell->flags >> CELLFLAG_EXTERNAL_SUPPLY_BIT;

	assert(p_cell->flags & CELLFLAG_IS_COMPUTING_MASK);

	if (p_cell->flags & CELLFLAG_IS_COMPUTED_MASK)
		return 0;

	if (p_cell->flags & (0xFFFFFF00 | CELLFLAG_EXTERNAL_SUPPLY_MASK)) {
		struct gridaddr addr;
		(void)gridcell_get_gridpage_and_full_addr(p_cell, &addr);

		int32_t x = (int32_t)addr.x;
		int32_t y = (int32_t)addr.y;

		if (p_stats->num_cells++ == 0) {
			p_stats->max_x = x;
			p_stats->max_y = y;
			p_stats->min_x = x;
			p_stats->min_y = y;
		} else {
			if (x < p_stats->min_x)
				p_stats->min_x = x;
			else if (x > p_stats->max_x)
				p_stats->max_x = x;
			if (y < p_stats->min_y)
				p_stats->min_y = y;
			else if (y > p_stats->max_y)
				p_stats->max_y = y;
		}
	}

	for (i = 0; i < EDGE_DIR_NUM; i++) {
		int edge_flags = (p_cell->flags >> (8 + 3*i)) & 0x7;
		struct gridcell *p_neighbour;
		if (edge_flags == EDGE_TYPE_NOTHING || edge_flags == EDGE_TYPE_SENDER)
			continue;
		p_stats->num_edgeops++;
		p_neighbour = gridcell_get_edge_neighbour(p_cell, i, 0);
		assert(p_neighbour != NULL);
		if (edge_flags == EDGE_TYPE_RECEIVER_DF)
			value |= (p_neighbour->flags >> CELLFLAG_DELAY_VALUE_BIT);
		else if (edge_flags == EDGE_TYPE_RECEIVER_DI) 
			value |= (~p_neighbour->flags >> CELLFLAG_DELAY_VALUE_BIT);
		else {
			if (grid_cell_solve(p_neighbour, p_stats))
				return 1;
			if (edge_flags == EDGE_TYPE_RECEIVER_F)
				value |= (p_neighbour->flags >> CELLFLAG_CURRENT_VALUE_BIT);
			else
				value |= (~p_neighbour->flags >> CELLFLAG_CURRENT_VALUE_BIT);
		}
	}

	p_cell->flags |= CELLFLAG_IS_COMPUTED_MASK;

	for (i = 0; i < VERTEX_DIR_NUM; i++) {
		if ((p_cell->flags >> (26 + i)) & 1) {
			if (i < 3)
				p_stats->num_vertops++;
			if (solve_net_recursive(gridcell_get_vertex_neighbour(p_cell, i, 0), &value, p_stats))
				return 1;
		}
	}

	if (value)
		*p_value |= value;

	return 0;
}


static void propagate_solution(struct gridcell *p_cell, uint64_t value_bit) {
	int i;

	assert(p_cell->flags & CELLFLAG_IS_COMPUTED_MASK);

	if ((p_cell->flags & CELLFLAG_IS_COMPUTING_MASK) == 0)
		return;

	p_cell->flags = (p_cell->flags & ~(CELLFLAG_IS_COMPUTING_MASK | CELLFLAG_CURRENT_VALUE_MASK)) | value_bit;

	for (i = 0; i < VERTEX_DIR_NUM; i++) {
		if ((p_cell->flags >> (26 + i)) & 1)
			propagate_solution(gridcell_get_vertex_neighbour(p_cell, i, 0), value_bit);
	}
}

static int grid_cell_solve(struct gridcell *p_cell, struct solve_stats *p_stats) {
	unsigned i;
	uint32_t netval;

	/* The computing mask bit is set - there is a loop */
	if (p_cell->flags & CELLFLAG_IS_COMPUTING_MASK)
		return 1;

	/* Already computed - nothing to do */
	if (p_cell->flags & CELLFLAG_IS_COMPUTED_MASK)
		return 0;

	/* Mark all cells in this net as being computed. */
	mark_net_as_computing_mask(p_cell);

	/* Solve the net */
	netval = 0;
	if (solve_net_recursive(p_cell, &netval, p_stats))
		return 1;

	/* Clear the flag, set the shared value */
	propagate_solution(p_cell, (((uint64_t)netval) & 1) << CELLFLAG_CURRENT_VALUE_BIT);

	return 0;
}

int grid_solve_recursive(struct gridpage *p_root, struct solve_stats *p_stats) {
	int ret = 0;
	int i;
	if (p_root == NULL)
		return 0;
	for (i = 0; i < PAGE_XY_NB*PAGE_XY_NB; i++) {
		if ((p_root->data[i].flags & (CELLFLAG_IS_COMPUTING_MASK | CELLFLAG_IS_COMPUTED_MASK)) == 0)
			ret |= grid_cell_solve(&(p_root->data[i]), p_stats);
	}
	for (i = 0; i < CELL_LOOKUP_NB; i++)
		ret |= grid_solve_recursive(p_root->lookups[i], p_stats);
	return ret;
}


static void grid_reset_compute_flags(struct gridpage *p_root) {
	int i;
	if (p_root == NULL)
		return;
	for (i = 0; i < PAGE_XY_NB*PAGE_XY_NB; i++)
		p_root->data[i].flags &= ~(uint64_t)(CELLFLAG_IS_COMPUTING_MASK | CELLFLAG_IS_COMPUTED_MASK);
	for (i = 0; i < CELL_LOOKUP_NB; i++)
		grid_reset_compute_flags(p_root->lookups[i]);
}

static void update_delay_states(struct gridpage *p_root) {
	int i;
	if (p_root == NULL)
		return;
	for (i = 0; i < PAGE_XY_NB*PAGE_XY_NB; i++) {
		uint64_t old_flags = p_root->data[i].flags;
		uint64_t delay_data = ((old_flags >> CELLFLAG_CURRENT_VALUE_BIT) & 1) << CELLFLAG_DELAY_VALUE_BIT;
		p_root->data[i].flags = (old_flags & ~(uint64_t)CELLFLAG_DELAY_VALUE_MASK) | delay_data;
	}
	for (i = 0; i < CELL_LOOKUP_NB; i++)
		update_delay_states(p_root->lookups[i]);
}

int grid_solve(struct gridstate *p_gs) {
	int errors;
	/* Clear the computed and is computing bits of all cells */
	grid_reset_compute_flags(p_gs->p_root);
	/* Recursively solve */
	p_gs->stats.max_x = 0;
	p_gs->stats.max_y = 0;
	p_gs->stats.min_x = 0;
	p_gs->stats.min_y = 0;
	p_gs->stats.num_edgeops = 0;
	p_gs->stats.num_vertops = 0;
	p_gs->stats.num_cells   = 0;
	p_gs->stats_ok = 0;
	errors = grid_solve_recursive(p_gs->p_root, &(p_gs->stats));
	/* Update delay states */
	if (!errors) {
		update_delay_states(p_gs->p_root);
		p_gs->stats_ok = 1;
	}
	return errors;
};

#define STORAGE_FLAG_MASK (CELLFLAG_EXTERNAL_SUPPLY_MASK | 0xFFFFFF00)

static int grid_save_rec(FILE *p_f, struct gridpage *p_page) {
	int i;

	for (i = 0; i < PAGE_XY_NB*PAGE_XY_NB; i++) {
		uint64_t storage_flags;
		struct gridaddr addr;

		if ((storage_flags = p_page->data[i].flags & STORAGE_FLAG_MASK) == 0)
			continue;

		addr.x = p_page->position.x | (i & PAGE_X_MASK);
		addr.y = p_page->position.y | (i >> PAGE_XY_BITS);

		if (fwrite(&addr, sizeof(addr), 1, p_f) != 1 || fwrite(&storage_flags, sizeof(storage_flags), 1, p_f) != 1)
			return 1;
	}

	for (i = 0; i < CELL_LOOKUP_NB; i++)
		if (p_page->lookups[i] != NULL)
			if (grid_save_rec(p_f, p_page->lookups[i]))
				return 1;

	return 0;
}

static int grid_save(struct gridstate *p_state, const char *p_filename) {
	FILE *p_f = fopen(p_filename, "wb");
	int failed = 0;
	if (p_f == NULL)
		return 1;
	if (p_state->p_root != NULL)
		failed = grid_save_rec(p_f, p_state->p_root);
	return fclose(p_f) || failed;
}

static int grid_load_file(struct gridstate *p_grid, FILE *p_f) {
	do {
		uint64_t storage_flags;
		struct gridaddr addr;
		struct gridcell *p_cell;
		int i;

		if (fread(&addr, sizeof(addr), 1, p_f) != 1 || fread(&storage_flags, sizeof(storage_flags), 1, p_f) != 1)
			return 0;

		printf("read %ld,%ld,%llu\n", (long)(int32_t)addr.x, (long)(int32_t)addr.y, (unsigned long long)storage_flags);
		if ((p_cell = gridstate_get_gridcell(p_grid, &addr, 1)) == NULL)
			return 1;

		for (i = 0; i < VERTEX_DIR_NUM; i++) {
			int ctl = (storage_flags >> (26 + i)) & 0x1;
			if (gridcell_set_vert_flags(p_cell, i, ctl))
				return 1;
		}

		for (i = 0; i < EDGE_DIR_NUM; i++) {
			int ctl = (storage_flags >> (8 + 3*i)) & 0x7;
			if (ctl != EDGE_TYPE_SENDER && gridcell_set_edge_flags(p_cell, i, ctl))
				return 1;
		}

	} while (1);
	return 1;
}

static int grid_load(struct gridstate *p_state, const char *p_filename) {
	FILE *p_f = fopen(p_filename, "rb");
	int failed = 0;
	if (p_f == NULL)
		return 1;
	failed = grid_load_file(p_state, p_f);
	fclose(p_f);
	return failed;
}


int gridpage_dump(struct gridpage *p_page, unsigned tree_location, int *p_nodes) {
	int i, j;
	int depth = 0;

	if (p_page == NULL)
		return 0;

	(*p_nodes)++;

#if 0
	printf("BEGIN PAGE 0x%08x (offset %d, %d)\n", tree_location, p_page->position.x, p_page->position.y);

	for (i = 0; i < PAGE_XY_NB; i++) {
		for (j = 0; j < PAGE_XY_NB; j++) {
			printf((p_page->data[i*PAGE_XY_NB+j].flags != i*PAGE_XY_NB+j) ? "*" : " ");
		}
		printf("\n");
	}
#endif

	for (i = 0; i < CELL_LOOKUP_NB; i++) {
		int x = gridpage_dump(p_page->lookups[i], (tree_location << CELL_LOOKUP_BITS) | i, p_nodes);
		if (x > depth)
			depth = x;
	}

#if 0
	printf("END PAGE 0x%08x\n", tree_location);
#endif

	return depth + 1;
}

#if 0
int64_t floor_div(int64_t a, int64_t b) {
    int64_t d = a / b;
    return (d * b == a) ? d : (d - ((a < 0) ^ (b < 0)));
}
#endif

#define EDGE_NOTHING      (0)
#define EDGE_SENDING_F    (1)
#define EDGE_SENDING_I    (2)
#define EDGE_SENDING_DF   (3)
#define EDGE_SENDING_DI   (4)
#define EDGE_RECEIVING_F  (5)
#define EDGE_RECEIVING_I  (6)
#define EDGE_RECEIVING_DF (7)
#define EDGE_RECEIVING_DI (8)

static int get_edge_connection_type(struct gridcell *p_cell, int edge_direction) {
	struct gridcell *p_neighbour;
	switch (gridcell_get_edge_flags(p_cell, edge_direction)) {
		case EDGE_TYPE_RECEIVER_F: return EDGE_RECEIVING_F;
		case EDGE_TYPE_RECEIVER_I: return EDGE_RECEIVING_I;
		case EDGE_TYPE_RECEIVER_DF: return EDGE_RECEIVING_DF;
		case EDGE_TYPE_RECEIVER_DI: return EDGE_RECEIVING_DI;
		case EDGE_TYPE_SENDER:
			p_neighbour = gridcell_get_edge_neighbour(p_cell, edge_direction, 0);
			assert(p_neighbour != NULL);
			switch (gridcell_get_edge_flags(p_neighbour, edge_dir_get_opposing(edge_direction))) {
				case EDGE_TYPE_RECEIVER_F: return EDGE_SENDING_F;
				case EDGE_TYPE_RECEIVER_I: return EDGE_SENDING_I;
				case EDGE_TYPE_RECEIVER_DF: return EDGE_SENDING_DF;
				case EDGE_TYPE_RECEIVER_DI: return EDGE_SENDING_DI;
				default:
					abort();
			}
		case EDGE_TYPE_NOTHING:
			break;
		default:
			abort();
	}
	return EDGE_NOTHING;
}

static void draw_edge_arrows(int edge_mode_ne, float px, float py, float dvecx, float dvecy, float radius) {
	static const float r30x = 0.866f;
	static const float r30y = 0.5f;

	ImDrawList *p_list = ImGui::GetWindowDrawList();

	ImDrawListFlags old_flags = p_list->Flags;
	//p_list->Flags = ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLines;

#define SHAPE_SIZE (0.42f)
#define ARROW_SIZE (1.1f)

	if (edge_mode_ne) {
		float mx1 = px + dvecx * (radius*(1.0f-ARROW_SIZE*0.5f));
		float my1 = py + dvecy * (radius*(1.0f-ARROW_SIZE*0.5f));
		float mx2 = px + dvecx * (radius*(1.0f+ARROW_SIZE*0.5f));
		float my2 = py + dvecy * (radius*(1.0f+ARROW_SIZE*0.5f));

		float nvecx  = r30x * dvecx - r30y * dvecy;
		float nvecy  = r30x * dvecy + r30y * dvecx;
		float nvecx2 = r30x * dvecx + r30y * dvecy;
		float nvecy2 = r30x * dvecy - r30y * dvecx;

		float ax1, ay1, ax2, ay2;

		ImU32 c = ImColor(192, 192, 128, 255);

		if  (   edge_mode_ne == EDGE_RECEIVING_DF
			||  edge_mode_ne == EDGE_RECEIVING_F
			||  edge_mode_ne == EDGE_RECEIVING_DI
			||  edge_mode_ne == EDGE_RECEIVING_I
			) {
			ax1 = mx1 + nvecx * radius * SHAPE_SIZE;
			ay1 = my1 + nvecy * radius * SHAPE_SIZE;
			ax2 = mx1 + nvecx2 * radius * SHAPE_SIZE;
			ay2 = my1 + nvecy2 * radius * SHAPE_SIZE;

			if  (   edge_mode_ne == EDGE_RECEIVING_DF
				||  edge_mode_ne == EDGE_RECEIVING_F
				)
				p_list->AddTriangleFilled(ImVec2(mx1, my1), ImVec2(ax1, ay1), ImVec2(ax2, ay2), c);
			else
				p_list->AddTriangle(ImVec2(mx1, my1), ImVec2(ax1, ay1), ImVec2(ax2, ay2), c, 2.0f * radius / 40.0f);

			mx1 = (ax1 + ax2) * 0.5f;
			my1 = (ay1 + ay2) * 0.5f;
		} else if (edge_mode_ne != EDGE_NOTHING) {
			ax1 = mx2 - nvecx * radius * SHAPE_SIZE;
			ay1 = my2 - nvecy * radius * SHAPE_SIZE;
			ax2 = mx2 - nvecx2 * radius * SHAPE_SIZE;
			ay2 = my2 - nvecy2 * radius * SHAPE_SIZE;

			if  (   edge_mode_ne == EDGE_SENDING_DF
				||  edge_mode_ne == EDGE_SENDING_F
				)
				p_list->AddTriangleFilled(ImVec2(mx2, my2), ImVec2(ax1, ay1), ImVec2(ax2, ay2), c);
			else
				p_list->AddTriangle(ImVec2(mx2, my2), ImVec2(ax1, ay1), ImVec2(ax2, ay2), c, 2.0f * radius / 40.0f);

			mx2 = (ax1 + ax2) * 0.5f;
			my2 = (ay1 + ay2) * 0.5f;
		}

		if  (   edge_mode_ne == EDGE_SENDING_DF
			||  edge_mode_ne == EDGE_SENDING_DI
			) {
			float r = radius * SHAPE_SIZE * 0.5f;
			p_list->AddCircle(ImVec2(mx1 + dvecx * r, my1 + dvecy * r), r, c, 11, 2.0f * radius / 40.0f);
			mx1 += dvecx * r * 2.0f;
			my1 += dvecy * r * 2.0f;
		
		} else if
			(   edge_mode_ne == EDGE_RECEIVING_DF
			||  edge_mode_ne == EDGE_RECEIVING_DI
			) {
			float r = radius * SHAPE_SIZE * 0.5f;
			p_list->AddCircle(ImVec2(mx2 - dvecx * r, my2 - dvecy * r), r, c, 11, 2.0f * radius / 40.0f);
			mx2 -= dvecx * r * 2.0f;
			my2 -= dvecy * r * 2.0f;
		}

		assert(isnormal(mx1));
		assert(isnormal(my1));
		assert(isnormal(mx2));
		assert(isnormal(my2));

		p_list->AddLine(ImVec2(mx1, my1), ImVec2(mx2, my2), c, 2.0f * radius / 40.0f);
	}


	p_list->Flags = old_flags;
}

#include "imgui_internal.h"

#define ASR(n_, b_) ((n_) >> (b_))

#define SQRT3   (1.732050807568877f)
#define SQRT3_4 (SQRT3 * 0.5f)

int get_cursor_hex_addr(struct gridaddr *p_addr, int64_t bl_x, int64_t bl_y, float cursor_x, float cursor_y, ImVec2 *prcc) {
	int64_t thx     = bl_x + (int32_t)(cursor_x * (65536.0f / 3.0f));
	int64_t thy     = bl_y + (int32_t)(cursor_y * (65536.0f / SQRT3_4));

	int64_t wholex  = ASR(thx, 16);
	int64_t wholey  = ASR(thy, 17) * 2;

	float   fbl_x   = ((float)(int32_t)(((uint32_t)thx) & 0xFFFF)) * (3.0f / 65536.0f);
	float   fbl_y   = ((float)(int32_t)(((uint32_t)thy) & 0x1FFFF)) * (SQRT3_4 / 65536.0f);

	float y_dbot    = fbl_y;
	float y_dmid    = fbl_y - SQRT3_4;
	float y_dtop    = fbl_y - 2.0f*SQRT3_4;
	float x_dleft   = fbl_x;
	float x_dmid    = fbl_x - 1.5f;
	float x_dright  = fbl_x - 3.0f;

	float y_dbot2   = y_dbot * y_dbot;
	float y_dmid2   = y_dmid * y_dmid;
	float y_dtop2   = y_dtop * y_dtop;
	float x_dleft2  = x_dleft * x_dleft;
	float x_dmid2   = x_dmid * x_dmid;
	float x_dright2 = x_dright * x_dright;

	int32_t offset_x = 0;
	int32_t offset_y = 0;
	int64_t out_x, out_y;

	float e_cur = x_dleft2 + y_dbot2;
	float t;

	/* top-left */
	t = x_dleft2 + y_dtop2;
	if (t < e_cur) {
		e_cur    = t;
		offset_x = 0;
		offset_y = 2;
		fbl_x    = x_dleft;
		fbl_y    = y_dtop;
	}

	/* top-right */
	t = x_dright2 + y_dtop2;
	if (t < e_cur) {
		e_cur    = t;
		offset_x = 1;
		offset_y = 2;
		fbl_x    = x_dright;
		fbl_y    = y_dtop;
	}

	/* bottom-right */
	t = x_dright2 + y_dbot2;
	if (t < e_cur) {
		e_cur    = t;
		offset_x = 1;
		offset_y = 0;
		fbl_x    = x_dright;
		fbl_y    = y_dbot;
	}

	/* middle */
	t = x_dmid2 + y_dmid2;
	if (t < e_cur) {
		e_cur    = t;
		offset_x = 0;
		offset_y = 1;
		fbl_x    = x_dmid;
		fbl_y    = y_dmid;
	}

	out_x = wholex + offset_x;
	out_y = wholey + offset_y;

	if (out_x < INT32_MIN || out_x > INT32_MAX || out_y < INT32_MIN || out_y > INT32_MAX)
		return 1;

	prcc->x   = fbl_x;
	prcc->y   = fbl_y;
	p_addr->x = (uint32_t)out_x;
	p_addr->y = (uint32_t)out_y;

	return 0;
}


ImVec2 iv2_add(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }
ImVec2 iv2_sub(ImVec2 a, ImVec2 b) { return ImVec2(a.x - b.x, a.y - b.y); }

struct plot_grid_state {
	float radius;
	int64_t bl_x; /* scaled by 16 bits */
	int64_t bl_y; /* scaled by 16 bits */
	
	int    mouse_down;
	ImVec2 mouse_down_pos;


};

struct highlighted_cell {
	struct gridaddr addr;
	ImVec2          pos_in_cell;

	int edge_idx;
	int b_close_to_edge;
	
};

static void vmpy(float *p_x, float *p_y, float x2, float y2) {
	float x1 = *p_x;
	float y1 = *p_y;
	*p_x = x1 * x2 - y1 * y2;
	*p_y = x1 * y2 + y1 * x2;
}

void plot_grid(struct gridstate *p_st, struct plot_grid_state *p_state)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

	ImVec2 graph_size;
	float radius = p_state->radius;
	graph_size.x = ImGui::CalcItemWidth();
	graph_size.y = style.FramePadding.y * 2 + 700;

    const ImRect frame_bb(window->DC.CursorPos, iv2_add(window->DC.CursorPos, graph_size));
    const ImRect inner_bb(iv2_add(frame_bb.Min, style.FramePadding), iv2_sub(frame_bb.Max, style.FramePadding));
    const ImRect total_bb(frame_bb.Min, frame_bb.Max);
    ImGui::ItemSize(total_bb, style.FramePadding.y);

	const ImGuiID id = window->GetID((void *)p_st);
    if (!ImGui::ItemAdd(total_bb, id, &frame_bb, 0))
        return;

	ImGuiIO& io = ImGui::GetIO();
    const bool hovered = ImGui::ItemHoverable(inner_bb, id);

	ImVec2 mprel = ImVec2(io.MousePos.x - inner_bb.Min.x, inner_bb.Max.y - io.MousePos.y);

	int64_t bl_x = p_state->bl_x;
	int64_t bl_y = p_state->bl_y;

	struct highlighted_cell hc;
	struct highlighted_cell *p_hc = NULL;

	int errors = 0;

	if (!get_cursor_hex_addr(&(hc.addr), bl_x, bl_y, mprel.x / radius,  mprel.y / radius, &(hc.pos_in_cell))) {
		float rat = hc.pos_in_cell.y / hc.pos_in_cell.x;
		hc.edge_idx = EDGE_DIR_N;

		float xp  = hc.pos_in_cell.x;
		float yp  = hc.pos_in_cell.y;

		if (hc.pos_in_cell.x > 0) {
			if (rat > 1.732) {
				hc.edge_idx = EDGE_DIR_N;    /* -0.5,0.866 -> -0.5, 0.866 */
			} else if (rat < -1.732) {
				hc.edge_idx = EDGE_DIR_S;    /* 0.5,-0.866 -> -0.5, 0.866 */
				vmpy(&xp, &yp, -1.0f, 0.0f);
			} else if (rat > 0.0f) {
				hc.edge_idx = EDGE_DIR_NE;
				vmpy(&xp, &yp, 0.5f, 0.866f); /* 0.5,0.866 -> -0.5,0.866 */
			} else {
				hc.edge_idx = EDGE_DIR_SE;
				vmpy(&xp, &yp, -0.5f, 0.866f); /* 1.0,0.0 -> -0.5,0.866 */
			}
		} else {
			if (rat > 1.732) {
				hc.edge_idx = EDGE_DIR_S;
				vmpy(&xp, &yp, -1.0f, 0.0f);
			} else if (rat < -1.732) {
				hc.edge_idx = EDGE_DIR_N;
			} else if (rat > 0.0f) {
				hc.edge_idx = EDGE_DIR_SW;
				vmpy(&xp, &yp, -0.5f, -0.866f); /* 1.0,0.0 -> -0.5,0.866 */
			} else {
				hc.edge_idx = EDGE_DIR_NW;
				vmpy(&xp, &yp, 0.5f, -0.866f); /* 1.0,0.0 -> -0.5,0.866 */
			}
		}

		hc.b_close_to_edge = fabsf(yp - 0.866f) < 0.16f;



		p_hc = &hc;
	}




	if (hovered) {
		int make_active = 0;

		if (g.IO.MouseDown[0] && !p_state->mouse_down) {
			p_state->mouse_down_pos = mprel;
			p_state->mouse_down = 1;
			make_active = 1;
		}

		if (g.IO.MouseDoubleClicked[0] && p_hc != NULL) {
			struct gridcell *p_cell = gridstate_get_gridcell(p_st, &(p_hc->addr), 0);
			if (p_cell != NULL) {
				int i;
				for (i = 0; i < EDGE_DIR_NUM; i++)
					gridcell_set_edge_flags(p_cell, i, EDGE_TYPE_NOTHING);
			}
		}

		if (g.IO.MouseClicked[1] && p_hc != NULL) {
			struct gridcell *p_cell = gridstate_get_gridcell(p_st, &(p_hc->addr), 1);
			if (p_cell != NULL) {
				if (p_hc->b_close_to_edge) {
					int get_edge_dir;
					int virt_dir;
					struct gridcell *p_nb;
					switch (p_hc->edge_idx) {
					case EDGE_DIR_N:  get_edge_dir = EDGE_DIR_NE; virt_dir = VERTEX_DIR_W; break;
					case EDGE_DIR_NE: get_edge_dir = EDGE_DIR_SE; virt_dir = VERTEX_DIR_NW; break;
					case EDGE_DIR_SE: get_edge_dir = EDGE_DIR_S;  virt_dir = VERTEX_DIR_NE; break;
					case EDGE_DIR_S:  get_edge_dir = EDGE_DIR_SW; virt_dir = VERTEX_DIR_E; break;
					case EDGE_DIR_SW: get_edge_dir = EDGE_DIR_NW; virt_dir = VERTEX_DIR_SE; break;
					case EDGE_DIR_NW: get_edge_dir = EDGE_DIR_N;  virt_dir = VERTEX_DIR_SW; break;
					default:
						abort();
					}
					if ((p_nb = gridcell_get_edge_neighbour(p_cell, get_edge_dir, 1)) != NULL)
						gridcell_set_vert_flags(p_nb, virt_dir, !gridcell_get_vert_flags(p_nb, virt_dir));

				} else {
					struct gridcell *p_nb;
					int new_flag;
					if ((p_nb = gridcell_get_edge_neighbour(p_cell, p_hc->edge_idx, 1)) != NULL) {
						switch (gridcell_get_edge_flags(p_nb, edge_dir_get_opposing(p_hc->edge_idx))) {
						case EDGE_TYPE_NOTHING:
							new_flag = EDGE_TYPE_RECEIVER_F;
							break;
						case EDGE_TYPE_RECEIVER_F:
							new_flag = EDGE_TYPE_RECEIVER_I;
							break;
						case EDGE_TYPE_RECEIVER_I:
							new_flag = EDGE_TYPE_RECEIVER_DF;
							break;
						case EDGE_TYPE_RECEIVER_DF:
							new_flag = EDGE_TYPE_RECEIVER_DI;
							break;
						default:
							new_flag = EDGE_TYPE_NOTHING;
							break;
						}
						gridcell_set_edge_flags_adv(p_nb, p_cell, edge_dir_get_opposing(p_hc->edge_idx), new_flag);
					}
				}

			}
		}




		if (make_active) {
			ImGui::SetActiveID(id, window);
			ImGui::SetFocusID(id, window);
			ImGui::FocusWindow(window);
		}
	}

	if (p_state->mouse_down) {
		ImVec2 drag = iv2_sub(mprel, p_state->mouse_down_pos);

		bl_x -= drag.x / (radius * 3.0f) * 65536.0f;
		bl_y -= drag.y / (radius * 0.866f) * 65536.0f;

		if (!g.IO.MouseDown[0]) {
			ImGui::ClearActiveID();
			p_state->mouse_down = 0;
			p_state->bl_x = bl_x;
			p_state->bl_y = bl_y;
		}
	}

	if (hovered) {
		if (io.MouseWheel != 0.0f) {
			bl_x -= -mprel.x / (radius * 3.0f) * 65536.0f;
			bl_y -= -mprel.y / (radius * 0.866f) * 65536.0f;

			radius *= powf(2.0, io.MouseWheel / 40.0f);
			if (radius < 3.0f)
				radius = 3.0f;
			p_state->radius = radius;

			bl_x -= +mprel.x / (radius * 3.0f) * 65536.0f;
			bl_y -= +mprel.y / (radius * 0.866f) * 65536.0f;

			p_state->bl_x = bl_x;
			p_state->bl_y = bl_y;
		}
	}

	errors = grid_solve(p_st);


    ImGui::RenderFrame(frame_bb.Min, frame_bb.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);

	ImDrawList *p_list = ImGui::GetWindowDrawList();

    p_list->PushClipRect(inner_bb.Min, inner_bb.Max, true);  // Render-level scissoring. This is passed down to your render function but not used for CPU-side coarse clipping. Prefer using higher-level ImGui::PushClipRect() to affect logic (hit-testing and widget culling)

	ImVec2 vMin = inner_bb.Min;
	ImVec2 vMax = inner_bb.Max;

	float window_width = vMax.x - vMin.x;
	float window_height = vMax.y - vMin.y;

	uint32_t cell_y = ((uint64_t)bl_y) >> 16;
	do {
		int64_t cy = ((int64_t)(int32_t)cell_y) * 65536;
		float oy = (cy - bl_y) * radius * (0.866f) / 65536.0f;

		if (oy - radius > window_height)
			break;

		uint32_t cell_x = ((uint64_t)bl_x) >> 16;

		do {
			int64_t cx = ((int64_t)(int32_t)cell_x) * 65536;
			float ox = (cx - bl_x) * radius * (3.0f) / 65536.0f;

			if (ox - radius > window_width)
				break;

			if (cell_y & 0x1) {
				ox += radius * 1.5f;
			}

			ImVec2 p;

			p.x = vMin.x + ox;
			p.y = vMax.y - oy;

			int edge_mode_n = EDGE_NOTHING;
			int edge_mode_ne = EDGE_NOTHING;
			int edge_mode_se = EDGE_NOTHING;

			int tunnel_n = 0;
			int tunnel_ne = 0;
			int tunnel_se = 0;

			struct gridaddr addr;
			addr.x = cell_x;
			addr.y = cell_y;

			struct gridcell *p_cell = gridstate_get_gridcell(p_st, &addr, 0);

			if (p_cell != NULL) {
				struct gridcell *p_n;
				edge_mode_n  = get_edge_connection_type(p_cell, EDGE_DIR_N);
				edge_mode_ne = get_edge_connection_type(p_cell, EDGE_DIR_NE);
				edge_mode_se = get_edge_connection_type(p_cell, EDGE_DIR_SE);

				p_n = gridcell_get_edge_neighbour(p_cell, EDGE_DIR_NE, 0);
				if (p_n != NULL) {
					tunnel_n = gridcell_get_vert_flags(p_n, VERTEX_DIR_W);
					tunnel_se = gridcell_get_vert_flags(p_n, VERTEX_DIR_SW);
				}

				p_n = gridcell_get_edge_neighbour(p_cell, EDGE_DIR_SE, 0);
				if (p_n != NULL) {
					tunnel_ne = gridcell_get_vert_flags(p_n, VERTEX_DIR_NW);
				}


			}

				/* draw the segments marked X */

				/*    XXX       ___             (X,y)
					*   /   X     /   \
					*  / 0,0 X___/ 1,0 \___
					*  \     X   \     /   \
					*   \___X 0,1 \___/ 1,1 \
					*   /   \     /   \     /
					*  / 0,2 \___/ 1,2 \___/
					*  \     /   \     /   \
					*   \___/ 0,3 \___/ 1,3 \
					*       \     /   \     /
					*        \___/     \___/
					*/

			ImVec2 inner_n1  = ImVec2(p.x - 0.5f * radius, p.y - 0.866f * radius);
			ImVec2 inner_n2  = ImVec2(p.x + 0.5f * radius, p.y - 0.866f * radius);
			ImVec2 inner_ne1 = ImVec2(p.x + 0.5f * radius, p.y - 0.866f * radius);
			ImVec2 inner_ne2 = ImVec2(p.x + 1.0f * radius, p.y);
			ImVec2 inner_se1 = ImVec2(p.x + 1.0f * radius, p.y);
			ImVec2 inner_se2 = ImVec2(p.x + 0.5f * radius, p.y + 0.866f * radius);

			if (tunnel_n) {
				inner_n1.x  -= 0.2f * radius;
				inner_n2.x  += 0.2f * radius;
			}
			if (tunnel_ne) {
				inner_ne1.x -= 0.2f * 0.5f * radius;
				inner_ne1.y -= 0.2f * 0.866f * radius;
				inner_ne2.x += 0.2f * 0.5f * radius;
				inner_ne2.y += 0.2f * 0.866f * radius;
			}
			if (tunnel_se) {
				inner_se1.x += 0.2f * 0.5f * radius;
				inner_se1.y -= 0.2f * 0.866f * radius;
				inner_se2.x -= 0.2f * 0.5f * radius;
				inner_se2.y += 0.2f * 0.866f * radius;
			}

			ImU32 cjoined = ImColor(255, 255, 255, 255);
			ImU32 cunjoined = ImColor(255, 255, 255, 128);
			

			p_list->AddLine(inner_n1, inner_n2, tunnel_n ? cjoined : cunjoined,    (2 + tunnel_n * 3) * radius / 40);
			p_list->AddLine(inner_ne1, inner_ne2, tunnel_ne ? cjoined : cunjoined, (2 + tunnel_ne * 3) * radius / 40);
			p_list->AddLine(inner_se1, inner_se2, tunnel_se ? cjoined : cunjoined, (2 + tunnel_se * 3) * radius / 40);

			draw_edge_arrows(edge_mode_n, p.x, p.y, 0.0f, -0.866f, radius);
			draw_edge_arrows(edge_mode_ne, p.x, p.y, 0.75f, -0.433f, radius);
			draw_edge_arrows(edge_mode_se, p.x, p.y, 0.75f, +0.433f, radius);

			if (cell_x == 0 && cell_y == 0)
				p_list->AddCircle(p, radius * 0.5, ImColor(192, 0, 0, 255), 17, 2.0f * radius / 40);



			if (p_cell != NULL) {
				if (p_cell->flags & CELLFLAG_IS_COMPUTING_MASK)
					p_list->AddCircleFilled(p, radius * 0.5, ImColor(255, 0, 128, 255), 17);
				else if (p_cell->flags & CELLFLAG_CURRENT_VALUE_MASK)
					p_list->AddCircleFilled(p, radius * 0.1, ImColor(0, 255, 0, 255), 17);

			}




			cell_x += 1;
		} while (1);

		cell_y += 1;

	} while (1);

	p_list->PopClipRect();

	if (ImGui::IsItemHovered() && p_hc != NULL)
	{
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);

		ImGui::Text("down=%d (lda=%f,%f) snap=(%ld,%ld) cell=(%f,%f) edge=%d errors=%d vert=%d", p_state->mouse_down, mprel.x, mprel.y, (long)(int32_t)p_hc->addr.x, (long)(int32_t)p_hc->addr.y, p_hc->pos_in_cell.x, p_hc->pos_in_cell.y, p_hc->edge_idx, errors, p_hc->b_close_to_edge);

		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}




int main(int argc, char **argv)
{
/*
	int i;
	for (i = 0; i < 10000000; i++) {
		uint32_t x = rand();
		uint32_t y = rand();
		uint64_t id = xy_to_id(x, y);
		uint32_t rx, ry;
		id_to_xy(id, &rx, &ry);
		//printf("%llu,%u,%u,%u,%u\n", id, x, rx, y, ry);
		if (rx != x || ry != y)
			abort();
	}
*/
#if 0
	int i;
	int directions[100];
	struct gridaddr pos;
	pos.x = 0;
	pos.y = 0;
	for (i = 0; i < 100; i++) {

		directions[i] = rand() & 0x7;
		if (gridaddr_edge_neighbour(&pos, &pos, directions[i]))
			abort();
		printf("%d,%d,%d\n", i, pos.x, pos.y);
	}
	for (i = 0; i < 100; i++) {
		if (gridaddr_edge_neighbour(&pos, &pos, dir_get_opposing(directions[(i * 13) % 100])))
			abort();
		printf("%d,%d,%d\n", i, pos.x, pos.y);
	}
	if (pos.x != 0 || pos.y != 0)
		abort();

#endif
	struct gridstate  gs;
	gridstate_init(&gs);

#if 0
	struct gridaddr   addr;
	struct gridcell  *p_cell;
	int i;
	uint32_t pdir = 0;


	addr.x = 0;
	addr.y = 0;
	if ((p_cell = gridstate_get_gridcell(&gs, &addr, 1)) == NULL)
		abort();

	for (i = 0; i < 30; i++) {
		struct gridcell *p_neighbour;
		int edge_ctl;
		int edge_dir = (pdir = (pdir * 16 + 112 * (rand() % EDGE_DIR_NUM) + 64) / 128);
		int virt_dir = rand() % VERTEX_DIR_NUM;
		int virt_ctl = rand() & 1;

		if ((p_neighbour = gridcell_get_vertex_neighbour(p_cell, virt_dir, 1)) == NULL)
			abort();
		gridcell_set_vert_flags_adv(p_cell, p_neighbour, virt_dir, virt_ctl);

		if ((p_neighbour = gridcell_get_edge_neighbour(p_cell, edge_dir, 1)) == NULL)
			abort();
		
		switch (rand() % 5) {
			case 0: edge_ctl = EDGE_TYPE_NOTHING; break;
			case 1: edge_ctl = EDGE_TYPE_RECEIVER_DF; break;
			case 2: edge_ctl = EDGE_TYPE_RECEIVER_DI; break;
			case 3: edge_ctl = EDGE_TYPE_RECEIVER_F; break;
			case 4: edge_ctl = EDGE_TYPE_RECEIVER_I; break;
		}

		gridcell_set_edge_flags_adv(p_cell, p_neighbour, edge_dir, edge_ctl);

		(void)gridcell_get_gridpage_and_full_addr(p_cell, &addr);
		//printf("set edge_flags for node %08x,%08x vdir=%d,%d\n", addr.x, addr.y, virt_dir, virt_ctl);
		p_cell = p_neighbour;
	}

	i = 0;
	printf("DEPTH = %d\n", gridpage_dump(gs.p_root, 1, &i));
	printf("NODES = %d\n", i);
#else
	if (grid_load(&gs, "startup.grid")) {
		printf("failed to load from startup.grid\n");
	} else {
		printf("loaded startup.grid\n");
	}

#endif

	// Setup window
	glfwSetErrorCallback(glfw_error_callback);
	if (!glfwInit())
		return 1;

	// Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
	// GL ES 2.0 + GLSL 100
	const char* glsl_version = "#version 100";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
	// GL 3.2 + GLSL 150
	const char* glsl_version = "#version 150";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
#else
	// GL 3.0 + GLSL 130
	const char* glsl_version = "#version 130";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	//glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
	//glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
#endif

	// Create window with graphics context
	GLFWwindow* window = glfwCreateWindow(1280, 720, "Dear ImGui GLFW+OpenGL3 example", NULL, NULL);
	if (window == NULL)
		return 1;
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1); // Enable vsync

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init(glsl_version);

	// Load Fonts
	// - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
	// - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
	// - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
	// - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
	// - Read 'docs/FONTS.md' for more instructions and details.
	// - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
	//io.Fonts->AddFontDefault();
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
	//ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
	//IM_ASSERT(font != NULL);

	// Our state
	bool show_demo_window = true;
	bool show_another_window = false;
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	struct plot_grid_state plot_state;
	plot_state.radius = 40.0f;
	plot_state.bl_x = -0 * 65536;
	plot_state.bl_y = -0 * 65536;
	plot_state.mouse_down = 0;

	// Main loop
	while (!glfwWindowShouldClose(window))
	{
		// Poll and handle events (inputs, window resize, etc.)
		// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
		// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
		// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
		// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
		glfwPollEvents();

		// Start the Dear ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		// 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
		if (show_demo_window)
			ImGui::ShowDemoWindow(&show_demo_window);

		// 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
		{
			static float f = 0.0f;
			static int counter = 0;

			ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

			ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
			ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
			ImGui::Checkbox("Another Window", &show_another_window);

			ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
			ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

			if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
				counter++;
			ImGui::SameLine();
			ImGui::Text("counter = %d", counter);

			ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
			ImGui::End();

		}

		// 3. Show another simple window.
		if (show_another_window)
		{
			ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
			ImGui::Text("Hello from another window!");
			if (ImGui::Button("Close Me"))
				show_another_window = false;
			ImGui::End();
		}


		ImGui::Begin("Designer");   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
		plot_grid(&gs, &plot_state);
		if (gs.stats_ok) {
			int64_t width = gs.stats.max_x - (int64_t)gs.stats.min_x + 1;
			int64_t height = gs.stats.max_y - (int64_t)gs.stats.min_y + 1;
			ImGui::SameLine();
			ImGui::BeginGroup();
			ImGui::Text("Grid Width: %lld", (long long)width);
			ImGui::Text("Grid Height: %lld", (long long)height);
			ImGui::Text("Grid Area: %lld", (long long)width * height);
			ImGui::Text("Total Cells: %ld", (long)gs.stats.num_cells);
			ImGui::Text("Area Efficiency %f", gs.stats.num_cells * 100.0f / (float)(width * height));
			ImGui::Text("Num Edge Connections: %ld", (long)gs.stats.num_edgeops);
			ImGui::Text("Num Vertex Connections: %ld", (long)gs.stats.num_vertops);
			ImGui::EndGroup();

		}
		ImGui::End();

		// Rendering
		ImGui::Render();
		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		glfwSwapBuffers(window);
	}

	if (argc > 1) {
		if (grid_save(&gs, argv[1])) {
			printf("failed to save grid to %s\n", argv[1]);
		} else {
			printf("saved grid to %s\n", argv[1]);
		}
	}

	// Cleanup
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}