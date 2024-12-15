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
#define CELL_LOOKUP_NB   (1 << CELL_LOOKUP_BITS)
#define CELL_LOOKUP_MASK (CELL_LOOKUP_NB - 1u)

#define PAGE_XY_BITS    (4)
#define PAGE_XY_NB      (1 << PAGE_XY_BITS)
#define PAGE_XY_MASK    (PAGE_XY_NB - 1)
#define PAGE_INDEX_MASK (PAGE_XY_NB*PAGE_XY_NB - 1)

struct gridaddr {
	/* 31 bits. */
	uint32_t y;

	/* 31 bits. */
	uint32_t x;

	/* 2 bits - only values 0-2 used */
	uint32_t z;

};

#define URANGE_CHECK(a, max_inclusive) (assert((a) <= (max_inclusive)), (a))

/* Convert a grid-address to a page address and return the cell index within the page. */
static uint32_t gridaddr_split(struct gridaddr *p_page_addr, const struct gridaddr *p_addr) {
	p_page_addr->x = (URANGE_CHECK(p_addr->x, 0x7FFFFFFF) & ~(uint32_t)PAGE_XY_MASK);
	p_page_addr->y = (URANGE_CHECK(p_addr->y, 0x7FFFFFFF) & ~(uint32_t)PAGE_XY_MASK);
	p_page_addr->z = 0;
	return
		(p_addr->x & PAGE_XY_MASK) |
		((p_addr->y & PAGE_XY_MASK) << PAGE_XY_BITS) |
		(URANGE_CHECK(p_addr->z, 2) << (2*PAGE_XY_BITS));
}

static int gridaddr_is_page_addr(const struct gridaddr *p_addr) {
	return
		 (URANGE_CHECK(p_addr->x, 0x7FFFFFFF) & (uint32_t)PAGE_XY_MASK) == 0 &&
		 (URANGE_CHECK(p_addr->y, 0x7FFFFFFF) & (uint32_t)PAGE_XY_MASK) == 0 &&
		 URANGE_CHECK(p_addr->z, 2) == 0;
}

static int gridaddr_add_check(struct gridaddr *p_dest, const struct gridaddr *p_src, int32_t xoffset, int32_t yoffset) {
	uint32_t x = URANGE_CHECK(p_src->x, 0x7FFFFFFF) + (uint32_t)xoffset;
	uint32_t y = URANGE_CHECK(p_src->y, 0x7FFFFFFF) + (uint32_t)yoffset;
	if ((x & 0x80000000) || (y & 0x80000000))
		return 1;
	p_dest->x = x;
	p_dest->y = y;
	p_dest->z = p_src->z;
	return 0;
}

#define EDGE_DIR_N   (0)
#define EDGE_DIR_NE  (1)
#define EDGE_DIR_SE  (2)
#define EDGE_DIR_NW  (3)
#define EDGE_DIR_SW  (4)
#define EDGE_DIR_S   (5)
#define EDGE_DIR_NUM (6)
static int get_opposing_edge_id(int dir) {
	assert(dir < EDGE_DIR_NUM);
	return 5 - dir;
}

#define NUM_LAYERS (3)

#define EDGE_LAYER_CONNECTION_UNCONNECTED             (0) /* 000 */
#define EDGE_LAYER_CONNECTION_SENDS                   (1) /* 001 */
#define EDGE_LAYER_CONNECTION_RECEIVES_DELAY_INVERTED (2) /* 010 */
#define EDGE_LAYER_CONNECTION_RECEIVES_INVERTED       (3) /* 011 */
#define EDGE_LAYER_CONNECTION_NET_CONNECTED           (4) /* 100 */

#define GRIDCELL_BIT_MERGED_LAYERS   (0x0000000010000000ull)
#define GRIDCELL_BIT_FORCE_NET       (0x0000000020000000ull)

#define GRIDCELL_PROGRAM_NET_ID_BITS            (0xFFFFFF0000000000ull)
#define GRIDCELL_PROGRAM_NET_ID_BITS_SET(x_)    (((uint64_t)(x_)) << 40)
#define GRIDCELL_PROGRAM_NET_ID_BITS_GET(x_)    ((x_) >> 40)
#define GRIDCELL_PROGRAM_BROKEN_BITS            (0x000000FC00000000ull)
#define GRIDCELL_PROGRAM_BROKEN_BIT_MASK(edge_) (((uint64_t)0x400000000) << (edge_))
#define GRIDCELL_PROGRAM_BUSY_BIT               (0x0000000200000000ull)


#define GRIDCELL_PROGRAM_BITS        (GRIDCELL_PROGRAM_NET_ID_BITS|GRIDCELL_PROGRAM_BROKEN_BITS|GRIDCELL_PROGRAM_BUSY_BIT)


/* tests if any cell edges are not disconnected, the layers are merged or the
 * FORCE_NET bit is set. if this is set, the cell is guaranteed to have a net
 * assigned to it. If it is not set, the cell definitely will not have a net
 * assigned to it. */
#define CELL_WILL_GET_A_NET(data_)   (((data_) & (GRIDCELL_BIT_MERGED_LAYERS | GRIDCELL_BIT_FORCE_NET | 0xFFFFC00)) != 0)


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
	static const int32_t y_offsets[6] =
		{   2   /* N */
		,   1   /* NE */
		,  -1   /* SE */
		,   1   /* NW */
		,  -1   /* SW */
		,  -2   /* S */
		};
	static const int32_t x_offsets[6][2] =
		/*  y_even  y_odd */
		{   {0,     0} /* N */
		,   {0,     1} /* NE */
		,   {0,     1} /* SE */
		,   {-1,    0} /* NW */
		,   {-1,    0} /* SW */
		,   {0,     0} /* S */
		};
	assert(edge_direction < EDGE_DIR_NUM);
	return gridaddr_add_check(p_dest, p_src, x_offsets[edge_direction][p_src->y & 1], y_offsets[edge_direction]);
}

struct gridcell {
	uint64_t data;

	/* LSB
	 *
	 * Used to identify the address of a cell within a gridpage. Can be used
	 * to get a pointer to the gridpage from the gridcell (which always exists
	 * inside of a gridpage.)
	 * 0:9  - is the index of the cell within the page (PAGE_XY_BITS (x) + PAGE_XY_BITS (y) + 2 (z) bits)
	 *
	 * Edge property bits.
	 * 10:12 - N_EDGE
	 * 13:15 - NE_EDGE
	 * 16:18 - SE_EDGE
	 * 19:21 - NW_EDGE
	 * 22:24 - SW_EDGE
	 * 25:27 - S_EDGE
	 *
	 * Set if the cell is merged with all cells above and below it. The flag
	 * is either set on none or all layers.
	 * 28    - MERGED_LAYERS
	 *
	 * This bit does absolutely nothing except guarantee that a net will be
	 * provided for this cell.
	 * 29    - FORCE_NET
	 *
	 * This bit is used as a temporary while creating a program. Once the
	 * program has been written this bit will be cleared. If the program
	 * fails to be created due to cycles, this bit will be set if the cell is
	 * part of a broken cycle. This is not ideal and it would be better to
	 * have 6 bits which identify the edge instead of the cell itself and we
	 * might change that later - for now, we have 32 bits for the net id.
	 * 33    - BUSY/BROKEN_NET
	 * 34:39 - BROKEN_EDGE (N, NE, SE, NW, SW, S)
	 * 40:63 - STORAGE_ID */

};

static void rt_assert_impl(int condition, const char *p_cond_str, const char *p_file, const int line) {
	if (!condition) {
		fprintf(stderr, "assertion failure(%s:%d): %s\n", p_file, line, p_cond_str);
		abort();
	}
}
#define RT_ASSERT_INNER(condition_, file_, line_) rt_assert_impl((condition_), #condition_, file_, line_)
#define RT_ASSERT(condition_)                     rt_assert_impl((condition_), #condition_, __FILE__, __LINE__)

struct gridpage {
	struct gridcell   data[PAGE_XY_NB*PAGE_XY_NB*NUM_LAYERS];
	struct gridstate *p_owner;
	struct gridpage  *lookups[CELL_LOOKUP_NB];
	struct gridaddr   position; /* position of data[0] in the full grid  */
	
};

#ifndef NDEBUG
static void verify_gridpage(struct gridpage *p_page, const char *p_file, const int line) {
	size_t i;
	RT_ASSERT_INNER(p_page->p_owner != NULL, p_file, line);
	RT_ASSERT_INNER((p_page->position.x & PAGE_XY_MASK) == 0, p_file, line);
	RT_ASSERT_INNER((p_page->position.y & PAGE_XY_MASK) == 0, p_file, line);
	for (i = 0; i < PAGE_XY_NB*PAGE_XY_NB*NUM_LAYERS; i++)
		RT_ASSERT_INNER((p_page->data[i].data & 0x3FF) == i, p_file, line);
}
#define DEBUG_CHECK_GRIDPAGE(p_page_) verify_gridpage(p_page_, __FILE__, __LINE__)
#else
#define DEBUG_CHECK_GRIDPAGE(p_page_) ((void)0)
#endif

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


/* Converts a grid address into a unique, scrambled and invertible
 * identifier. */
static uint64_t gridaddr_to_id(const struct gridaddr *p_addr) {
	uint64_t grp =
		(URANGE_CHECK((uint64_t)p_addr->z, 0x3) << 62) |
		(URANGE_CHECK((uint64_t)p_addr->y, 0x7FFFFFFF) << 31) |
		URANGE_CHECK(p_addr->x, 0x7FFFFFFF);
	uint64_t umix = grp*8249772677985670961ull;
	return (umix >> 48) ^ umix;
}

#if 0
static void id_to_xy(uint64_t id, uint32_t *p_x, uint32_t *p_y, uint32_t *p_z) {
	uint64_t umix = (id >> 48) ^ id;
	uint64_t grp = umix*7426732773883044305ull;
	*p_z = (uint32_t)(grp >> 62);
	*p_y = (uint32_t)((grp >> 31) & 0x7FFFFFFF);
	*p_x = (uint32_t)(grp & 0x7FFFFFFF);
	assert(*p_z < 3);
}
#endif



static struct gridpage *gridstate_get_gridpage(struct gridstate *p_grid, const struct gridaddr *p_page_addr, int permit_create) {
	struct gridpage **pp_c = &(p_grid->p_root);
	struct gridpage *p_c   = *pp_c;
	uint32_t page_x        = p_page_addr->x;
	uint32_t page_y        = p_page_addr->y;
	uint64_t page_id;
	size_t   i;

	assert(gridaddr_is_page_addr(p_page_addr));

	page_id = gridaddr_to_id(p_page_addr);

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
	for (i = 0; i < PAGE_XY_NB*PAGE_XY_NB*NUM_LAYERS; i++) {
		p_c->data[i].data = i;
	}

	DEBUG_CHECK_GRIDPAGE(p_c);

	*pp_c = p_c;
	return p_c;
}

static struct gridcell *gridstate_get_gridcell(struct gridstate *p_grid, const struct gridaddr *p_addr, int permit_create) {
	struct gridaddr page_addr;
	uint32_t cell_index = gridaddr_split(&page_addr, p_addr);
	struct gridpage *p_page = gridstate_get_gridpage(p_grid, &page_addr, permit_create);
	if (p_page == NULL)
		return NULL;
	return &(p_page->data[cell_index]);
}

static unsigned gridcell_get_page_index(const struct gridcell *p_gc) {
	return p_gc->data & 0x3FF;
}

static struct gridpage *gridcell_get_page_and_index(const struct gridcell *p_cell, uint32_t *p_page_index) {
	*p_page_index = gridcell_get_page_index(p_cell);
	return (struct gridpage *)(p_cell - *p_page_index);
}

/* Given a cell, find the page which it is part of */
static struct gridpage *gridcell_get_gridpage_and_full_addr(struct gridcell *p_cell, struct gridaddr *p_addr) {
	uint32_t         page_index;
	struct gridpage *p_page = gridcell_get_page_and_index(p_cell, &page_index);
	DEBUG_CHECK_GRIDPAGE(p_page);
	p_addr->x = p_page->position.x | (page_index & PAGE_XY_MASK);
	p_addr->y = p_page->position.y | ((page_index >> PAGE_XY_BITS) & PAGE_XY_MASK);
	p_addr->z = page_index >> (2*PAGE_XY_BITS);
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

static struct gridcell *gridcell_get_edge_neighbour(struct gridcell *p_cell, int edge_id, int permit_create) {
	struct gridaddr  neighbour_addr;
	struct gridpage *p_cell_page = gridcell_get_gridpage_and_full_addr(p_cell, &neighbour_addr);

	assert(edge_id < EDGE_DIR_NUM); /* there are no east and west edges in the hex grid, just as there are no north and south vertices */
	
	if (gridaddr_edge_neighbour(&neighbour_addr, &neighbour_addr, edge_id))
		return NULL;

	return gridpage_get_gridcell(p_cell_page, &neighbour_addr, permit_create);
}

static int gridcell_set_neighbour_edge_connection_type(struct gridcell *p_gc, int edge_id, int connection_type) {
	struct gridcell *p_neighbour = gridcell_get_edge_neighbour(p_gc, edge_id, 1);
	if (p_neighbour != NULL) {
		uint64_t cval;
		uint64_t mask = 0x7;
		uint64_t nval = connection_type;
		int      csft = 10 + edge_id*3;
		int      nsft = 10 + get_opposing_edge_id(edge_id)*3;

		if (connection_type == EDGE_LAYER_CONNECTION_UNCONNECTED || connection_type == EDGE_LAYER_CONNECTION_NET_CONNECTED) {
			cval = connection_type;
		} else {
			assert(connection_type == EDGE_LAYER_CONNECTION_RECEIVES_DELAY_INVERTED || connection_type == EDGE_LAYER_CONNECTION_RECEIVES_INVERTED);
			cval = EDGE_LAYER_CONNECTION_SENDS;
		}

		p_gc->data        = (p_gc->data & ~(mask << csft)) | (cval << csft);
		p_neighbour->data = (p_neighbour->data & ~(mask << nsft)) | (nval << nsft);
		return 0;
	}
	return 1;
}

static int gridcell_get_edge_connection_type(const struct gridcell *p_gc, int edge_id) {
	unsigned edge_props;
	assert(edge_id < EDGE_DIR_NUM);
	edge_props       = (p_gc->data >> (10 + 3*edge_id)) & 0x7;
	return URANGE_CHECK(edge_props, 4);
}

static int gridcell_get_neighbour_edge_connection_type(struct gridcell *p_gc, int edge_id) {
	struct gridcell *p_neighbour = gridcell_get_edge_neighbour(p_gc, edge_id, 0);
	if (p_neighbour == NULL)
		return EDGE_LAYER_CONNECTION_UNCONNECTED;
	return gridcell_get_edge_connection_type(p_neighbour, get_opposing_edge_id(edge_id));
}

static int gridcell_are_layers_fused_get(const struct gridcell *p_gc) {
	return (p_gc->data & GRIDCELL_BIT_MERGED_LAYERS) != 0;
}
static void gridcell_are_layers_fused_set(struct gridcell *p_gc, int b_value) {
	struct gridpage *p_page;
	uint32_t         page_index;
	uint32_t         i;

	if (b_value != 0 && (p_gc->data & GRIDCELL_BIT_MERGED_LAYERS) != 0)
		return;
	if (b_value == 0 && (p_gc->data & GRIDCELL_BIT_MERGED_LAYERS) == 0)
		return;

	p_page = gridcell_get_page_and_index(p_gc, &page_index);

	page_index &= 0xFF;

	for (i = 0; i < NUM_LAYERS; i++) {
		if (b_value) {
			p_page->data[page_index | (i << 8)].data |= GRIDCELL_BIT_MERGED_LAYERS;
		} else {
			p_page->data[page_index | (i << 8)].data &= ~GRIDCELL_BIT_MERGED_LAYERS;
		}
	}
}
static void gridcell_are_layers_fused_toggle(struct gridcell *p_gc) {
	gridcell_are_layers_fused_set(p_gc, !gridcell_are_layers_fused_get(p_gc));
}



struct program_net {
	uint32_t net_id;
	uint32_t first_cell_stack_index;
	uint32_t cell_count;
	uint32_t current_solve_cell;
	uint32_t current_solve_edge;
	uint32_t b_currently_in_solve_stack;
	uint32_t b_exists_in_a_cycle;

};

struct program {
	/* The program is made up of net_count blocks which define the value of that net.
	 *
	 * Each group consists of a count of how many sources exist. Followed by a pair
	 * of words for each source. The first word is the mode and the second is a source
	 * net. The modes are as follows:
	 *   * 0: invert the value of the given net
	 *   * 1: invert the value of the previous value of the given net
	 * 
	 * The supplied nets ids must be smaller than the currently active net for
	 * modes which require immediate value. This forces sequencing of operation.
	 * 
	 * It is permissible for count values to be zero in which case the data is not
	 * touched.
	 * 
	 * After the program has executed, the data buffer is copied into an old state
	 * buffer and the new buffer is zeroed. The caller may initialise specific data
	 * bits prior to execution to provide external input. */
	uint32_t *p_code;
	size_t    code_count;
	size_t    code_alloc_count;

	/* net list */
	struct program_net **pp_netstack;
	struct program_net  *p_nets;
	uint64_t            *p_data;
	uint64_t            *p_last_data;
	size_t               net_alloc_count;
	size_t               net_count;

	/* stack */
	void               **pp_stack;
	size_t               stack_count;
	size_t               stack_alloc_count;

};


#define U64MASKS(x_) \
	{x_((uint64_t)0x0000000000000001) \
	,x_((uint64_t)0x0000000000000002) \
	,x_((uint64_t)0x0000000000000004) \
	,x_((uint64_t)0x0000000000000008) \
	,x_((uint64_t)0x0000000000000010) \
	,x_((uint64_t)0x0000000000000020) \
	,x_((uint64_t)0x0000000000000040) \
	,x_((uint64_t)0x0000000000000080) \
	,x_((uint64_t)0x0000000000000100) \
	,x_((uint64_t)0x0000000000000200) \
	,x_((uint64_t)0x0000000000000400) \
	,x_((uint64_t)0x0000000000000800) \
	,x_((uint64_t)0x0000000000001000) \
	,x_((uint64_t)0x0000000000002000) \
	,x_((uint64_t)0x0000000000004000) \
	,x_((uint64_t)0x0000000000008000) \
	,x_((uint64_t)0x0000000000010000) \
	,x_((uint64_t)0x0000000000020000) \
	,x_((uint64_t)0x0000000000040000) \
	,x_((uint64_t)0x0000000000080000) \
	,x_((uint64_t)0x0000000000100000) \
	,x_((uint64_t)0x0000000000200000) \
	,x_((uint64_t)0x0000000000400000) \
	,x_((uint64_t)0x0000000000800000) \
	,x_((uint64_t)0x0000000001000000) \
	,x_((uint64_t)0x0000000002000000) \
	,x_((uint64_t)0x0000000004000000) \
	,x_((uint64_t)0x0000000008000000) \
	,x_((uint64_t)0x0000000010000000) \
	,x_((uint64_t)0x0000000020000000) \
	,x_((uint64_t)0x0000000040000000) \
	,x_((uint64_t)0x0000000080000000) \
	,x_((uint64_t)0x0000000100000000) \
	,x_((uint64_t)0x0000000200000000) \
	,x_((uint64_t)0x0000000400000000) \
	,x_((uint64_t)0x0000000800000000) \
	,x_((uint64_t)0x0000001000000000) \
	,x_((uint64_t)0x0000002000000000) \
	,x_((uint64_t)0x0000004000000000) \
	,x_((uint64_t)0x0000008000000000) \
	,x_((uint64_t)0x0000010000000000) \
	,x_((uint64_t)0x0000020000000000) \
	,x_((uint64_t)0x0000040000000000) \
	,x_((uint64_t)0x0000080000000000) \
	,x_((uint64_t)0x0000100000000000) \
	,x_((uint64_t)0x0000200000000000) \
	,x_((uint64_t)0x0000400000000000) \
	,x_((uint64_t)0x0000800000000000) \
	,x_((uint64_t)0x0001000000000000) \
	,x_((uint64_t)0x0002000000000000) \
	,x_((uint64_t)0x0004000000000000) \
	,x_((uint64_t)0x0008000000000000) \
	,x_((uint64_t)0x0010000000000000) \
	,x_((uint64_t)0x0020000000000000) \
	,x_((uint64_t)0x0040000000000000) \
	,x_((uint64_t)0x0080000000000000) \
	,x_((uint64_t)0x0100000000000000) \
	,x_((uint64_t)0x0200000000000000) \
	,x_((uint64_t)0x0400000000000000) \
	,x_((uint64_t)0x0800000000000000) \
	,x_((uint64_t)0x1000000000000000) \
	,x_((uint64_t)0x2000000000000000) \
	,x_((uint64_t)0x4000000000000000) \
	,x_((uint64_t)0x8000000000000000) \
	}

#define NOTHING(x_) x_
static uint64_t BIT_MASKS[] = U64MASKS(NOTHING);
#undef NOTHING
#if 0
#define INVERT(x_) ~x_
static uint64_t INV_BIT_MASKS[] = U64MASKS(INVERT);
#undef INVERT
#endif

/* Before running this, the caller can initialise any field input bits to
 * 1 in the p_data pointer. p_data is zeroed after every execution so
 * externally supplied bits must be written prior to calling program_run
 * every time. Also, neither p_data nor p_last_data can be cached, their
 * values will change after every call to program_run.
 * 
 * After running this, output data for each net is in p_last_data. */
void program_run(struct program *p_program) {
	/* run the program. */
	uint32_t *p_code     = p_program->p_code;
	uint64_t *p_data     = p_program->p_data;
	uint64_t *p_old_data = p_program->p_last_data;
	size_t    net_count  = p_program->net_count;
	size_t    dest_net;

	for (dest_net = 0; dest_net < net_count; dest_net++) {
		size_t    nb_sources   = *p_code++;
		uint64_t *p_dest       = &(p_data[dest_net >> 6]);
		uint64_t  dest_val     = *p_dest;
		uint64_t  dest_val_set = dest_val | (1ull << (dest_net & 0x3F));

		dest_val_set = (nb_sources) ? dest_val_set : dest_val; /* ensure no further reads if there are zero sources */

		while (dest_val != dest_val_set) {
			uint32_t  word       = *p_code++;
			uint32_t  src_net    = word & 0xFFFFFF;
			uint64_t *p_data_src = (word & 0x1000000) ? p_old_data : p_data;
			uint64_t  src_data   = (assert((word & 0x1000000) != 0 || (src_net < dest_net)), p_data_src[src_net >> 6]);
			uint64_t  src_val    = src_data; /* (word & 0x2000000) ? ~src_data : src_data; <<<<< MAYBE - DO WE WANT DIODES? */
			uint64_t  src_ctl    = src_val & (1ull << (src_net & 0x3F)); // & BIT_MASKS[src_net & 0x3F];
			dest_val     = (src_ctl) ? dest_val : dest_val_set; /* set the bit if the conditions have been met */
			dest_val_set = (--nb_sources) ? dest_val_set : dest_val; /* ensure termination */
			assert((word >> 24) < 2);
		}

		p_code += nb_sources;
		*p_dest = dest_val;
	}

	/* pointer jiggle and state reset. */
	p_program->p_data      = p_old_data;
	p_program->p_last_data = p_data;

	/* state reset. */
	memset(p_program->p_data, 0, ((p_program->net_count + 63)/64)*sizeof(uint64_t));
}

static
struct program_net *
program_net_push(struct program *p_program) {
	if (p_program->net_count >= p_program->net_alloc_count) {
		struct program_net *p_new_nets;
		struct program_net **pp_new_netstack;
		uint64_t           *p_new_data;
		size_t newsz      = ((p_program->net_count*4)/3) & ~(size_t)0xff;
		size_t data_words;
		if (newsz < 1024)
			newsz = 1024;
		data_words = (newsz + 63)/64;
		if ((p_new_nets = (struct program_net *)realloc(p_program->p_nets, sizeof(struct program_net)*newsz)) == NULL)
			abort();
		if ((pp_new_netstack = (struct program_net **)realloc(p_program->pp_netstack, sizeof(struct program_net *)*newsz)) == NULL)
			abort();
		if ((p_new_data = (uint64_t *)realloc(p_program->p_data, sizeof(uint64_t)*2*data_words)) == NULL)
			abort();
		p_program->net_alloc_count = newsz;
		p_program->pp_netstack     = pp_new_netstack;
		p_program->p_nets          = p_new_nets;
		p_program->p_data          = p_new_data;
		p_program->p_last_data     = &(p_new_data[data_words]);
	}
	return &(p_program->p_nets[p_program->net_count++]);
}

static void program_init(struct program *p_program) {
	p_program->p_code        = NULL;
	p_program->p_data        = NULL;
	p_program->p_last_data   = NULL;
	p_program->pp_stack      = NULL;
	p_program->p_nets        = NULL;
	p_program->pp_netstack   = NULL;

	p_program->code_alloc_count  = 0;
	p_program->stack_alloc_count = 0;
	p_program->net_alloc_count   = 0;

	p_program->code_count  = 0;
	p_program->stack_count = 0;
	p_program->net_count   = 0;

	(void)program_net_push(p_program);

	p_program->p_data[0]      = 0;
	p_program->p_last_data[0] = 0;

	p_program->net_count   = 0;
}

/* Returns true if the program is executable and program_run() can be called.
 * This function returns false after a failed call to program_compile() and
 * indicates that some of the cells will have the BROKEN bit set. */
static int program_is_valid(struct program *p_program) {
	return (p_program->net_count == 0) || (p_program->code_count > 0);
}

static void program_stack_push(struct program *p_program, void *p_ptr) {
	if (p_program->stack_count >= p_program->stack_alloc_count) {
		size_t newsz = ((p_program->stack_alloc_count*4)/3) & ~(size_t)0xff;
		void **pp_new_list;
		if (newsz < 1024)
			newsz = 1024;
		if ((pp_new_list = (void **)realloc(p_program->pp_stack, newsz*sizeof(void *))) == NULL)
			abort();
		p_program->pp_stack          = pp_new_list;
		p_program->stack_alloc_count = newsz;
	}
	p_program->pp_stack[p_program->stack_count++] = p_ptr;
}

static uint32_t *program_code_reserve(struct program *p_program) {
	if (p_program->code_count >= p_program->code_alloc_count) {
		size_t    newsz = ((p_program->code_count*4)/3) & ~(size_t)0xff;
		uint32_t *p_new_code;
		if (newsz < 1024)
			newsz = 1024;
		if ((p_new_code = (uint32_t *)realloc(p_program->p_code, newsz*sizeof(uint32_t))) == NULL)
			abort();
		p_program->p_code           = p_new_code;
		p_program->code_alloc_count = newsz;
	}
	return &(p_program->p_code[p_program->code_count++]);
}

static void move_cell_and_layers(struct gridcell **pp_list_base, uint32_t idx, uint32_t *p_insidx, uint64_t compute_flag_and_net_id_bits) {
	if (pp_list_base[idx]->data & GRIDCELL_BIT_MERGED_LAYERS) {
		uint32_t         page_index_base, i;
		struct gridpage *p_page = gridcell_get_page_and_index(pp_list_base[idx], &page_index_base);

		/* preserve the page x, y address while zeroing the layer */
		page_index_base &= 0xFF;

		for (i = 0; i < NUM_LAYERS; i++) {
			uint32_t page_index    = page_index_base | (i << 8);
			uint64_t index_in_list = GRIDCELL_PROGRAM_NET_ID_BITS_GET(p_page->data[page_index].data);
			uint32_t new_index     = (*p_insidx)++;

			/* if the merged layer bit is set on one layer, it must be set on all! */
			assert(p_page->data[page_index].data & GRIDCELL_BIT_MERGED_LAYERS);

			/* none of the layers are expected to have the busy bit set as we are only just about to set it. */
			assert((p_page->data[page_index].data & GRIDCELL_PROGRAM_BUSY_BIT) == 0);

			if (new_index != index_in_list) {
				struct gridcell *p_tmp            = pp_list_base[new_index];
				pp_list_base[new_index]           = pp_list_base[index_in_list];
				pp_list_base[index_in_list]       = p_tmp;

				/* todo: remember what this assert is for and write a comment. */
				assert((p_tmp->data & GRIDCELL_PROGRAM_BUSY_BIT) == 0);

				p_tmp->data = (p_tmp->data & ~GRIDCELL_PROGRAM_NET_ID_BITS) | GRIDCELL_PROGRAM_NET_ID_BITS_SET(index_in_list);
			}

			pp_list_base[new_index]->data     = (pp_list_base[new_index]->data & ~(GRIDCELL_PROGRAM_BUSY_BIT|GRIDCELL_PROGRAM_NET_ID_BITS)) | compute_flag_and_net_id_bits;
		}
	} else {
		uint32_t new_index     = (*p_insidx)++;

		assert((pp_list_base[idx]->data & GRIDCELL_PROGRAM_BUSY_BIT) == 0);

		if (new_index != idx) {
			struct gridcell *p_tmp  = pp_list_base[new_index];
			pp_list_base[new_index] = pp_list_base[idx];
			pp_list_base[idx]       = p_tmp;

			assert((p_tmp->data & GRIDCELL_PROGRAM_BUSY_BIT) == 0);
			p_tmp->data = (p_tmp->data & ~GRIDCELL_PROGRAM_NET_ID_BITS) | GRIDCELL_PROGRAM_NET_ID_BITS_SET(idx);
		}

		pp_list_base[new_index]->data = (pp_list_base[new_index]->data & ~(GRIDCELL_PROGRAM_BUSY_BIT|GRIDCELL_PROGRAM_NET_ID_BITS)) | compute_flag_and_net_id_bits;
	}
}

#define PROGRAM_DEBUG (0)

/* After calling program_compile, the upper 32 bits of each cell's data
 * correspond to the net id of that cell. This can be used to get the
 * the value of the net after running the program by looking at the
 * stored data. It can also be used to force values on in the program
 * data prior to execution. */
static int program_compile(struct program *p_program, struct gridstate *p_gridstate) {
	size_t            num_grid_pages;
	size_t            num_cells;
	struct gridcell **pp_list_base;
	int               num_broken_nets = 0;

	p_program->stack_count = 0;
	p_program->net_count   = 0;
	p_program->code_count  = 0;

	/* no nodes, no problems. */
	if (p_gridstate->p_root == NULL)
		return 0;

	/* 1) push all active grid-pages onto the stack. */
	{
		program_stack_push(p_program, p_gridstate->p_root);
		for (num_grid_pages = 0; num_grid_pages < p_program->stack_count; num_grid_pages++) {
			struct gridpage *p_gp = (struct gridpage *)p_program->pp_stack[num_grid_pages];
			int i;
			for (i = 0; i < CELL_LOOKUP_NB; i++) {
				if (p_gp->lookups[i]) {
					program_stack_push(p_program, p_gp->lookups[i]);
				}
			}
		}
	}

#if PROGRAM_DEBUG
	printf("added %llu grid pages\n", (unsigned long long)num_grid_pages);
#endif

	/* 2) push all used grid-cells onto the list. set their net id to the
	 * position in the list and clear the compute bit. */
	{
		size_t i;
		for (num_cells = 0, i = 0; i < num_grid_pages; i++) {
			struct gridpage *p_gp = (struct gridpage *)p_program->pp_stack[i];
			int j;
			for (j = 0; j < PAGE_XY_NB*PAGE_XY_NB*NUM_LAYERS; j++) {
				if (!CELL_WILL_GET_A_NET(p_gp->data[j].data))
					continue;
				/* Clear all upper bits related to program development and
				 * then set the net ID to be the index of the cell in the
				 * list. We need it to be this for the next step. */
				p_gp->data[j].data &= ~GRIDCELL_PROGRAM_BITS;
				p_gp->data[j].data |= GRIDCELL_PROGRAM_NET_ID_BITS_SET(num_cells);
				num_cells++;
				program_stack_push(p_program, &(p_gp->data[j]));
			}
		}

		/* note to future self - DO NOT CALL program_stack_push after this
		 * it could change the base pointer of pp_stack. */
		pp_list_base = (struct gridcell **)&(p_program->pp_stack[num_grid_pages]);
	}

#if PROGRAM_DEBUG
	printf("added %llu cells\n", (unsigned long long)num_cells);
#endif

	/* 3) sort the list into groups sharing a common net. reassign the net
	 * ids to be actual net ids. this will set all the compute bits. in this
	 * step, the GRIDCELL_PROGRAM_BUSY_BIT effectively idenfies if the net_id
	 * bits of the cell are an actual net id as opposed to the index of the
	 * cell in the list. At the end of this, all cells will have the
	 * GRIDCELL_PROGRAM_BUSY_BIT bit set. */
	{
		uint32_t group_pos;
		group_pos   = 0;
		while (group_pos < num_cells) {
			uint32_t            group_end                    = group_pos;
			uint64_t            net_id                       = p_program->net_count; /* must be read before calling program_net_push() */
			uint64_t            compute_flag_and_net_id_bits = GRIDCELL_PROGRAM_BUSY_BIT | GRIDCELL_PROGRAM_NET_ID_BITS_SET(net_id);
			struct program_net *p_net                        = program_net_push(p_program);
			p_net->net_id    = net_id;
			p_net->b_currently_in_solve_stack = 0;
			p_net->first_cell_stack_index = group_pos;
			p_net->b_exists_in_a_cycle = 0;
			/* Initialise the state such that on the first increment, we will
			 * move to cell 0, edge 0. */
			p_net->current_solve_cell = (uint32_t)-1;
			p_net->current_solve_edge = EDGE_DIR_NUM-1;
			move_cell_and_layers(pp_list_base, group_pos, &group_end, compute_flag_and_net_id_bits);
			do {
				int i;
				assert((pp_list_base[group_pos]->data & GRIDCELL_PROGRAM_BITS) == compute_flag_and_net_id_bits);
				for (i = 0; i < EDGE_DIR_NUM; i++) {
					int ctype = gridcell_get_edge_connection_type(pp_list_base[group_pos], i);
					if (ctype == EDGE_LAYER_CONNECTION_NET_CONNECTED) {
						struct gridcell *p_neighbour = gridcell_get_edge_neighbour(pp_list_base[group_pos], i, 0);
						if ((p_neighbour->data & GRIDCELL_PROGRAM_BUSY_BIT) == 0) {
							move_cell_and_layers(pp_list_base, GRIDCELL_PROGRAM_NET_ID_BITS_GET(p_neighbour->data), &group_end, compute_flag_and_net_id_bits);
						}
					}
				}
				group_pos++;
			} while (group_pos < group_end);
			p_net->cell_count = group_pos - p_net->first_cell_stack_index;
			p_net->first_cell_stack_index += num_grid_pages; /* ensure offset is applied past the grid pages at the start. */
		}
	}

	/* at this point, all cells are assigned to nets. */

	/* 4) order the nets such that they are all solvable and detect if the
	 * program is broken (has loops not broken by delays). */
	{
		size_t i;

		/* The stack array is used for two things. The part that grows up is
		 * used to track where we are in traversing the nets. The part that
		 * grows down is completely processed nets. As such, once this
		 * process ends, the nets can be evaluated from the end to the
		 * beginning of the list knowing that dependencies will be
		 * satisfied perfectly. */
		size_t end_of_stack_pointer = p_program->net_count;

		/* This is truely horrible - but seems to work. We first loop over
		 * nets - the purpose of this loop is to identify starting points
		 * which are nets that have not been processed. This could happen
		 * for a circuit which has distinct and disconnected sections. */
		for (i = 0; i < p_program->net_count; i++) {
			size_t stack_size;

			/* already solved? nothing to do here. */
			if (p_program->p_nets[i].current_solve_cell == p_program->p_nets[i].cell_count)
				continue;

			/* If it isn't fully solved, it had better be completely
			 * unsolved and never touched or this code is totally busted. */
			assert(p_program->p_nets[i].current_solve_cell == ((uint32_t)-1) && p_program->p_nets[i].current_solve_edge == (EDGE_DIR_NUM-1));

			/* If it is marked as being in the stack - something has gone
			 * completely wrong. */
			assert(p_program->p_nets[i].b_currently_in_solve_stack == 0);

			/* Okay, we have found a net that has not been seen before. We
			 * really only care about things that feed into this net, that
			 * is what we care about. If there are other nets that consume
			 * from this one, whatever, we can find it later in the outer
			 * for loop - no problem. But here, what we want to do is fully
			 * explore the nets that feed into recursively and without
			 * delay (delay always breaks cycles). Start by pushing this
			 * net onto the top of the stack and mark it as being in the
			 * stack (this is how we discover cycles and are able to mark
			 * all nets which take part in a cycle - if we have a stack
			 * of nodes poking outwards to a source and we hit a net which
			 * says it is already in the stack, there is some line of nets
			 * in the stack which form a loop). */
			stack_size                                      = 1;
			p_program->pp_netstack[0]                       = &(p_program->p_nets[i]);
			p_program->p_nets[i].b_currently_in_solve_stack = 1;

			while (stack_size) {
				/* Get the net on the very top of the stack. */
				struct program_net *p_this_net = p_program->pp_netstack[stack_size - 1];

				/* The very first thing we do is update the state to figure
				 * out what needs to happen during this update. Note that the
				 * first update of a state, these are initialised to values
				 * that will ensure that we start with edge 0 cell 0. */
				if (++p_this_net->current_solve_edge == EDGE_DIR_NUM) {
					p_this_net->current_solve_edge = 0;
					++p_this_net->current_solve_cell;
				}

				/* Then look to see if this node has now been completely
				 * traced. If it has, we can pop it to the backwards-growing
				 * end of the stack and it is completely done. This while()
				 * loop forms a bit of a state machine using the stack_size
				 * and the current_solve_cell and current_solve_edge members
				 * of the nets. Each iteration processes exactly one edge *or*
				 * discards one net from the stack (i.e. puts it in the
				 * finish list). */
				if (p_this_net->current_solve_cell == p_this_net->cell_count) {
					/* This net is finished - put at the end of the stack array */
					assert(p_this_net->current_solve_edge == 0);

					/* We're totally stuffed if somehow this got to zero. It
					 * means we visited something/somethings more than once. */
					assert(end_of_stack_pointer);

					/* Put the net at the end of the stack. Mark it as no
					 * longer in the list (other nodes can now reference this
					 * net whenever). */
					p_program->pp_netstack[--end_of_stack_pointer] = p_this_net;
					p_this_net->b_currently_in_solve_stack = 0; /* finished. */
					stack_size--;
				} else {
					/* The net on the top of the stack is not finished yet and
					 * we have more edges to process. */
					uint32_t            cell_idx;
					struct gridcell    *p_cell;
					int                 edge_id, edge_type;

					/* Either we should still be looking at edges in a valid
					 * cell within the net or we should be finished (which is
					 * handled in the block prior to the else). */
					assert(p_this_net->current_solve_cell < p_this_net->cell_count && p_this_net->current_solve_edge < EDGE_DIR_NUM);

					/* Get a pointer to the cell we are currently looking at
					 * within the current net and figure out what is going on
					 * with the current edge we are interested in. */
					cell_idx          = p_this_net->first_cell_stack_index + p_this_net->current_solve_cell;
					p_cell            = (struct gridcell *)p_program->pp_stack[cell_idx];
					edge_id           = p_this_net->current_solve_edge;
					edge_type         = gridcell_get_edge_connection_type(p_cell, edge_id);

					/* If the edge type is anything other than an undelayed,
					 * receiving mode, we don't need to do anything at all.
					 * If it is a net connection, we're going to look at it
					 * eventually (or have already looked at it). If it is a
					 * send connection, we don't care for reasons earlier
					 * mentioned. */
					if (edge_type == EDGE_LAYER_CONNECTION_RECEIVES_INVERTED) {
						/* Get the neighbouring cell and its net. */
						struct gridcell    *p_neighbour     = gridcell_get_edge_neighbour(p_cell, edge_id, 0);
						struct program_net *p_neighbour_net = &(p_program->p_nets[GRIDCELL_PROGRAM_NET_ID_BITS_GET(p_neighbour->data)]);

						/* The neighbour must exist if the edge type to it
						 * was connected. */
						assert(p_neighbour != NULL);

						/* The connection type should be send (as our cell
						 * said the edge was a receiver). */
						assert(gridcell_get_edge_connection_type(p_neighbour, get_opposing_edge_id(edge_id)) == EDGE_LAYER_CONNECTION_SENDS);

						if (p_neighbour_net->b_currently_in_solve_stack) {
							/* Go down the stack to find where the loop starts */
							size_t j = stack_size-1;
							while (p_program->pp_netstack[j] != p_neighbour_net) {
								assert(j);
								j--;
							}

							/* Go back up the stack marking broken edges along the way */
							for (; j < stack_size; j++) {
								struct program_net *p_stack_net;
								struct gridcell    *p_stack_cell;
								p_stack_net  = p_program->pp_netstack[j];
								p_stack_cell = (struct gridcell *)p_program->pp_stack[p_stack_net->first_cell_stack_index + p_stack_net->current_solve_cell];
								p_stack_cell->data |= GRIDCELL_PROGRAM_BROKEN_BIT_MASK(p_stack_net->current_solve_edge);
								gridcell_get_edge_neighbour(p_stack_cell, p_stack_net->current_solve_edge, 0)->data |= GRIDCELL_PROGRAM_BROKEN_BIT_MASK(get_opposing_edge_id(p_stack_net->current_solve_edge));
								p_stack_net->b_exists_in_a_cycle = 1;
							}

							num_broken_nets++;
						} else if (p_neighbour_net->current_solve_cell != p_neighbour_net->cell_count) {
							/* The net is not already in the stack and has not
							 * been solved (the assert verifies everything is
							 * as expected). We need to put it on the top of
							 * the stack and start the edge exploration
							 * processes on it. */
							assert(p_neighbour_net->current_solve_cell == ((uint32_t)-1) && p_neighbour_net->current_solve_edge == (EDGE_DIR_NUM-1));
							p_program->pp_netstack[stack_size++] = p_neighbour_net;
							p_neighbour_net->b_currently_in_solve_stack = 1;
						} else {
							/* The net is not on the stack but has already
							 * been completely processed earlier. We do not
							 * need to do anything :) yay */
						}
					}
				}
			}
		}
		assert(end_of_stack_pointer == 0);
	}

	/* At this point, if there are no broken nets, we could actually run the
	 * program by evaluating the net stack backwards and examining the cells.
	 * But now what we want to do is re-number our nets to be in order of
	 * execution. The busy bit now explicitly identifies if the cell is part
	 * of a net in a cycle. */
	{
		size_t i;
		for (i = 0; i < p_program->net_count; i++) {
			struct program_net *p_net = p_program->pp_netstack[p_program->net_count-1-i];
			uint32_t j;
			uint64_t new_id_mask = GRIDCELL_PROGRAM_NET_ID_BITS_SET(i) | (p_net->b_exists_in_a_cycle ? GRIDCELL_PROGRAM_BUSY_BIT : 0);
			for (j = 0; j < p_net->cell_count; j++) {
				uint32_t         cell_idx = p_net->first_cell_stack_index + j;
				struct gridcell *p_cell = (struct gridcell *)p_program->pp_stack[cell_idx];
				assert(GRIDCELL_PROGRAM_NET_ID_BITS_GET(p_cell->data) == p_net->net_id);
				p_cell->data = (p_cell->data & ~(GRIDCELL_PROGRAM_NET_ID_BITS|GRIDCELL_PROGRAM_BUSY_BIT)) | new_id_mask;
			}
			p_net->net_id = i;
		}
	}

#if PROGRAM_DEBUG
	{
		size_t i;
		printf("found %llu nets\n", (unsigned long long)p_program->net_count);
		printf("  netid     cell address (x, y, layer)\n");
		for (i = 0; i < p_program->net_count; i++) {
			uint32_t j;
			struct program_net *p_net = p_program->pp_netstack[p_program->net_count-1-i];
			printf("  net %llu (id=%llu is broken=%d):\n", (unsigned long long)i, (unsigned long long)p_net->net_id, p_net->b_exists_in_a_cycle);
			for (j = 0; j < p_net->cell_count; j++) {
				struct gridaddr addr;
				uint32_t cell_idx = p_net->first_cell_stack_index + j;
				(void)gridcell_get_gridpage_and_full_addr((struct gridcell *)(p_program->pp_stack[cell_idx]), &addr);
				printf
					("    (%ld, %ld, %ld)\n"
					,(long)addr.x - (long)0x40000000
					,(long)addr.y - (long)0x40000000
					,(long)addr.z
					);

			}
		}
	}
#endif

	/* Early exit before we write our solver code. */
	if (num_broken_nets) {
		return num_broken_nets;
	}

	/* Fun time - build the program. At this point, everything is totally
	 * great. pp_netstack is reverse ordered by dependencies. We have no loops
	 * we can just read the stack backwards, look at the cells and dump where
	 * they get their data from. */
	{
		size_t    i;
		for (i = 0; i < p_program->net_count; i++) {
			struct program_net *p_net = p_program->pp_netstack[p_program->net_count-1-i];
			uint32_t j;
			uint32_t num_sources = 0;
			uint32_t *p_code_start = program_code_reserve(p_program);
			assert(p_net->net_id == i);
			for (j = 0; j < p_net->cell_count; j++) {
				uint32_t         cell_idx = p_net->first_cell_stack_index + j;
				struct gridcell *p_cell = (struct gridcell *)p_program->pp_stack[cell_idx];
				int k;
				assert(GRIDCELL_PROGRAM_NET_ID_BITS_GET(p_cell->data) == i);
				for (k = 0; k < EDGE_DIR_NUM; k++) {
					int edge_type = gridcell_get_edge_connection_type(p_cell, k);
					if (edge_type == EDGE_LAYER_CONNECTION_RECEIVES_DELAY_INVERTED || edge_type == EDGE_LAYER_CONNECTION_RECEIVES_INVERTED) {
						struct gridcell *p_neighbour         = gridcell_get_edge_neighbour(p_cell, k, 0);
						uint32_t         neighbour_net       = GRIDCELL_PROGRAM_NET_ID_BITS_GET(p_neighbour->data);
						assert(gridcell_get_edge_connection_type(p_neighbour, get_opposing_edge_id(k)) == EDGE_LAYER_CONNECTION_SENDS);
						if (edge_type == EDGE_LAYER_CONNECTION_RECEIVES_INVERTED) {
							assert(neighbour_net < i);
							*(program_code_reserve(p_program)) = (0ull << 24) | neighbour_net;
						} else {
							assert(edge_type == EDGE_LAYER_CONNECTION_RECEIVES_DELAY_INVERTED);
							assert(neighbour_net < p_program->net_count);
							*(program_code_reserve(p_program)) = (1ull << 24) | neighbour_net;
						}
						num_sources++;
					}
				}
				*p_code_start = num_sources;
			}
		}

		/* clear states ready for processing. */
		memset(p_program->p_data, 0, sizeof(uint64_t)*((p_program->net_count + 63)/64));
		memset(p_program->p_last_data, 0, sizeof(uint64_t)*((p_program->net_count + 63)/64));
	}

#if PROGRAM_DEBUG
	{
		size_t    i;
		uint32_t *p_code = p_program->p_code;
		printf("program stored in %llu words (%llu bytes)\n", (unsigned long long)p_program->code_count, ((unsigned long long)(p_program->code_count*4)));
		for (i = 0; i < p_program->net_count; i++) {
			size_t nb_sources = *p_code++;
			if (nb_sources == 0) {
				printf("  net %llu has zero sources\n", (unsigned long long)i);
			} else {
				printf("  net %llu has %llu sources\n", (unsigned long long)i, (unsigned long long)nb_sources);
				while (nb_sources--) {
					uint32_t  word       = *p_code++;
					uint32_t  op         = word >> 24;
					uint32_t  src_net    = word & 0xFFFFFF;
					const char *p_opname;
					if (op == 0) {
						assert(src_net < i);
						p_opname = "         INVERT";
					} else {
						assert(src_net < p_program->net_count);
						assert(op == 1);
						p_opname = "INVERT PREVIOUS";
					}
					printf("    %s %llu\n", p_opname, (unsigned long long)src_net);
				}
			}
		}
	}
#endif

	return 0;
}

#include "imgui_internal.h"

#define SQRT3   (1.732050807568877f)
#define SQRT3_4 (SQRT3*0.5f)

ImVec2 iv2_add(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }
ImVec2 iv2_sub(ImVec2 a, ImVec2 b) { return ImVec2(a.x - b.x, a.y - b.y); }

struct plot_grid_state {
	float radius;
	int64_t bl_x; /* scaled by 16 bits */
	int64_t bl_y; /* scaled by 16 bits */
	
	int    mouse_down;
	int    b_mouse_down_pos_changed;
	ImVec2 mouse_down_pos;

#if 0
#define NUM_SNAKE_POINTS (2*2*2*2*2*3*5)
	float aa_the_snake[NUM_SNAKE_POINTS][2];
#endif

};

struct highlighted_cell {
	struct gridaddr addr;
	ImVec2          pos_in_cell;
	
};

static void vmpyadd(float *p_x, float *p_y, float x2, float y2, float offsetx, float offsety) {
	float x1 = *p_x;
	float y1 = *p_y;
	*p_x = x1*x2 - y1*y2 + offsetx;
	*p_y = x1*y2 + y1*x2 + offsety;
}

static void vmpy(float *p_x, float *p_y, float x2, float y2) {
	vmpyadd(p_x, p_y, x2, y2, 0.0f, 0.0f);
}

static ImVec2 imvec2_cmac(float ax, float ay, float x1, float y1, float x2, float y2) {
	vmpyadd(&x1, &y1, x2, y2, ax, ay);
	return ImVec2(x1, y1);
}


static int get_next_connection_type(int old) {
	switch (old) {
	case EDGE_LAYER_CONNECTION_RECEIVES_INVERTED:
		return EDGE_LAYER_CONNECTION_RECEIVES_DELAY_INVERTED;
	case EDGE_LAYER_CONNECTION_RECEIVES_DELAY_INVERTED:
		return EDGE_LAYER_CONNECTION_NET_CONNECTED;
	case EDGE_LAYER_CONNECTION_NET_CONNECTED:
		return EDGE_LAYER_CONNECTION_UNCONNECTED;
	default:
		assert(old == EDGE_LAYER_CONNECTION_UNCONNECTED || old == EDGE_LAYER_CONNECTION_SENDS);
		return EDGE_LAYER_CONNECTION_RECEIVES_INVERTED;
	}
}

struct visible_cell_info {
	struct gridaddr addr_l0;

	float    centre_pixel_x;
	float    centre_pixel_y;

	int      b_cursor_in_cell;
	float    cursor_relative_to_centre_x;
	float    cursor_relative_to_centre_y;


};

struct visible_cell_iterator {
	float scale;
	float invscale;
	float window_width;
	float window_height;
	float min_x;
	float max_y;
	float mouse_x;
	float mouse_y;
	int64_t bl_y;
	int64_t bl_x; 

	struct visible_cell_info info;

};

void
visible_cell_iterator_init
	(struct visible_cell_iterator *p_iter
	,int64_t bl_x
	,int64_t bl_y
	,const ImRect &inner_bb
	,float scale
	,const ImVec2 &cursor_pos
	) {
	ImVec2 vMin = inner_bb.Min;
	ImVec2 vMax = inner_bb.Max;
	p_iter->bl_x           = bl_x;
	p_iter->bl_y           = bl_y;
	p_iter->scale          = scale;
	p_iter->invscale       = 1.0f / scale;
	p_iter->window_width   = vMax.x - vMin.x;
	p_iter->window_height  = vMax.y - vMin.y;
	p_iter->info.addr_l0.y = ((uint64_t)bl_y) >> 16;
	p_iter->info.addr_l0.x = (((uint64_t)bl_x) >> 16) - 1; /* compensates for pre increment */
	p_iter->info.addr_l0.z = 0;
	p_iter->min_x          = vMin.x;
	p_iter->max_y          = vMax.y;
	p_iter->mouse_x        = cursor_pos.x;
	p_iter->mouse_y        = cursor_pos.y;
}

const struct visible_cell_info *visible_cell_iterator_next(struct visible_cell_iterator *p_iter) {
	do {
		int64_t cy, cx;
		float   oy, ox;
		float   mrpx, mrpy;

		cy = ((int64_t)(int32_t)p_iter->info.addr_l0.y)*65536;
		oy = (cy - p_iter->bl_y)*p_iter->scale*0.866f/65536.0f;
		if (oy - p_iter->scale > p_iter->window_height) {
			return NULL;
		}

		p_iter->info.addr_l0.x++;
		cx = ((int64_t)(int32_t)p_iter->info.addr_l0.x)*65536;
		ox = (cx - p_iter->bl_x)*p_iter->scale*3.0f/65536.0f;
		if (p_iter->info.addr_l0.y & 0x1)
			ox += p_iter->scale*1.5f;

		if (ox - p_iter->scale > p_iter->window_width) {
			p_iter->info.addr_l0.y++;
			p_iter->info.addr_l0.x = (((uint64_t)p_iter->bl_x) >> 16) - 1; /* compensates for pre increment */
			continue;
		}

		p_iter->info.centre_pixel_x = p_iter->min_x + ox;
		p_iter->info.centre_pixel_y = p_iter->max_y - oy;

		mrpx = (p_iter->mouse_x - p_iter->info.centre_pixel_x)*p_iter->invscale;
		mrpy = (p_iter->mouse_y - p_iter->info.centre_pixel_y)*p_iter->invscale;

		p_iter->info.b_cursor_in_cell =
			(   (mrpy >= -SQRT3_4 && mrpy <= SQRT3_4)
			&&  (   (mrpx >= -0.5f && mrpx <= 0.5f)
			    ||  (mrpx < -0.5f && fabsf(mrpy) < (1.0f + mrpx)*SQRT3)
			    ||  (mrpx > 0.5f && fabsf(mrpy) < (1.0f - mrpx)*SQRT3)
			    )
			);
		
		if (p_iter->info.b_cursor_in_cell) {
			p_iter->info.cursor_relative_to_centre_x = mrpx;
			p_iter->info.cursor_relative_to_centre_y = mrpy;
		} else {
			p_iter->info.cursor_relative_to_centre_x = 0.0f;
			p_iter->info.cursor_relative_to_centre_y = 0.0f;
		}

		return &(p_iter->info);
	} while (1);

	return NULL;
}

struct layer_edge_info {
	struct gridaddr addr;
	struct gridaddr addr_other;

	/* True if the cursor is somewhere inside addr. */
	int             b_mouse_in_addr;

	/* Only valid if b_mouse_in_addr is non-zero. Gives the scaled position
	 * of the cursor relative to the centre of the cell. The scaling ensures
	 * a value between -1 and 1. */
	float           cursor_relative_to_centre_x;

	/* Only valid if b_mouse_in_addr is non-zero. Gives the scaled position
	 * of the cursor relative to the centre of the cell. The scaling ensures
	 * a value between -sqrt(3/4) and sqrt(3/4). */
	float           cursor_relative_to_centre_y;

	/* If the cursor is specifically in the clickable region for the current
	 * edge and layer, this will be non-zero. If this is non-zero and
	 * b_mouse_in_addr is zero, the mouse is in addr_other. */
	int             b_mouse_over_either;

	/* centre pixel of the cell at addr */
	float           centre_pixel_x;
	float           centre_pixel_y;

	int             layer;

	/* the edge of addr we are looking at */
	int             edge;

};

struct layer_edge_iterator {
	struct visible_cell_iterator cell_iter;
	const struct visible_cell_info *p_info;
	float mpxrc_rs;
	float mpyrc_rs;

	struct layer_edge_info info;

};

void
layer_edge_iterator_init
	(struct layer_edge_iterator *p_iter
	,int64_t bl_x
	,int64_t bl_y
	,const ImRect &inner_bb
	,float scale
	,const ImVec2 &cursor_pos
	) {
	visible_cell_iterator_init(&(p_iter->cell_iter), bl_x, bl_y, inner_bb, scale, cursor_pos);
	p_iter->info.edge  = 2;
	p_iter->info.layer = 2;

}

/* Properties of our hexagons... */

#define HEXAGON_CENTRE_TO_VERTEX_DISTANCE                        (1.0f)
#define HEXAGON_EDGE_LENGTH                                      (HEXAGON_CENTRE_TO_VERTEX_DISTANCE)
#define HEXAGON_CENTRE_TO_EDGE_CENTRE_DISTANCE                   (SQRT3_4)
#define HEXAGON_INNER_CENTRE_TO_VERTEX_DISTANCE                  ((1.0f + SQRT3)/(2.0f + SQRT3)) /* ~0.73205 */
#define HEXAGON_INNER_EDGE_LENGTH                                (HEXAGON_INNER_CENTRE_TO_VERTEX_DISTANCE)
#define HEXAGON_INNER_CENTRE_TO_EDGE_CENTRE_DISTANCE             (HEXAGON_INNER_CENTRE_TO_VERTEX_DISTANCE*SQRT3_4) /* ~0.634 */
#define HEXAGON_INNER_TO_OUTER_EDGE_PARALLEL_DISTANCE            (HEXAGON_CENTRE_TO_EDGE_CENTRE_DISTANCE - HEXAGON_INNER_CENTRE_TO_EDGE_CENTRE_DISTANCE) /* ~0.2 - maybe exact? */

/* If each inner edge contains 3 points: one in the centre and the other two
 * spaced +/- this value, this is the unique value such that circles could be
 * drawn centred on each point with a radius of half this value such that they
 * would all touch without overlapping. */
#define HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE ((SQRT3 + 3.0f)/(10.0f + 6.0f*SQRT3)) /* k = ~0.23205 */

static const float AA_INNER_EDGE_CENTRE_POINTS[3][2] =
	{{-HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE, -(HEXAGON_CENTRE_TO_EDGE_CENTRE_DISTANCE - HEXAGON_INNER_TO_OUTER_EDGE_PARALLEL_DISTANCE)}
	,{0.0f,                                                      -(HEXAGON_CENTRE_TO_EDGE_CENTRE_DISTANCE - HEXAGON_INNER_TO_OUTER_EDGE_PARALLEL_DISTANCE)}
	,{HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE,  -(HEXAGON_CENTRE_TO_EDGE_CENTRE_DISTANCE - HEXAGON_INNER_TO_OUTER_EDGE_PARALLEL_DISTANCE)}
	};

static const float AA_NEIGHBOUR_CENTRE_OFFSETS[3][2] = {{0.0f, -SQRT3}, {1.5f, -SQRT3_4}, {1.5f, SQRT3_4}};

static const float AA_EDGE_ROTATORS[6][2] =
	{{ 1.0f,  0.0f}
	,{ 0.5f,  SQRT3_4}
	,{-0.5f,  SQRT3_4}
	,{-1.0f,  0.0f}
	,{-0.5f, -SQRT3_4}
	,{ 0.5f, -SQRT3_4}
	};

//#define HEXAGON_OPTIMAL_GAP                                      (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE/2.8f)

const struct layer_edge_info *layer_edge_iterator_next(struct layer_edge_iterator *p_iter) {
	do {
		if (p_iter->info.layer >= NUM_LAYERS - 1) {
			if (p_iter->info.edge >= EDGE_DIR_NUM/2 - 1) {
				if ((p_iter->p_info = visible_cell_iterator_next(&(p_iter->cell_iter))) == NULL)
					return NULL;

				p_iter->mpxrc_rs = (p_iter->cell_iter.mouse_x - p_iter->p_info->centre_pixel_x)*p_iter->cell_iter.invscale;
				p_iter->mpyrc_rs = (p_iter->cell_iter.mouse_y - p_iter->p_info->centre_pixel_y)*p_iter->cell_iter.invscale;
				p_iter->info.edge = 0;
			} else {
				p_iter->info.edge++;
				vmpy(&(p_iter->mpxrc_rs), &(p_iter->mpyrc_rs), 0.5f, -SQRT3_4);
			}
			p_iter->info.layer = 0;
		} else {
			p_iter->info.layer++;
		}

		p_iter->info.addr.x         = p_iter->p_info->addr_l0.x;
		p_iter->info.addr.y         = p_iter->p_info->addr_l0.y;
		p_iter->info.addr.z         = p_iter->info.layer;

		if (!gridaddr_edge_neighbour(&(p_iter->info.addr_other), &(p_iter->info.addr), p_iter->info.edge)) {
			float touching_radius = HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f;
			float xrc             = p_iter->mpxrc_rs - (p_iter->info.layer - 1)*HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE;
			float yrc             = p_iter->mpyrc_rs + (0.866f - HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE);
			float yrc2            = p_iter->mpyrc_rs + (0.866f + HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE);

			p_iter->info.centre_pixel_x              = p_iter->p_info->centre_pixel_x;
			p_iter->info.centre_pixel_y              = p_iter->p_info->centre_pixel_y;
			p_iter->info.b_mouse_in_addr             = p_iter->p_info->b_cursor_in_cell;
			p_iter->info.cursor_relative_to_centre_x = p_iter->p_info->cursor_relative_to_centre_x;
			p_iter->info.cursor_relative_to_centre_y = p_iter->p_info->cursor_relative_to_centre_y;
			p_iter->info.b_mouse_over_either =
				(   xrc > -touching_radius
				&&  xrc < touching_radius
				&&  (  (yrc > -HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE && yrc < 0.0f) /* in the square between the inner and outer hexagon edges */
					||  xrc*xrc + yrc*yrc < touching_radius*touching_radius /* in the circular landing shape at the end of the hexagon */
					|| (yrc2 > 0.0 && yrc2 < HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE) /* in the square between the inner and outer hexagon edges */
					||  xrc*xrc + yrc2*yrc2 < touching_radius*touching_radius /* in the circular landing shape at the end of the hexagon */
					)
				);

			return &(p_iter->info);
		}
	} while (1);
	return NULL;
}

static
void
draw_inverted_delayed_arrow
	(float centre_x, float centre_y, float pre_offset_x, float scale_rotate_x, float scale_rotate_y, ImU32 colour) {
	float       ay = -(HEXAGON_CENTRE_TO_EDGE_CENTRE_DISTANCE - HEXAGON_INNER_TO_OUTER_EDGE_PARALLEL_DISTANCE);
	float       by = -(HEXAGON_CENTRE_TO_EDGE_CENTRE_DISTANCE + HEXAGON_INNER_TO_OUTER_EDGE_PARALLEL_DISTANCE);
	float       tr = (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE/2.8f);
	ImVec2      a_points[19];
	int         i;
	ImDrawList *p_list = ImGui::GetWindowDrawList();
	a_points[0] = imvec2_cmac(centre_x, centre_y, pre_offset_x + (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f/2.8f), -HEXAGON_CENTRE_TO_EDGE_CENTRE_DISTANCE+HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.25f, scale_rotate_x, scale_rotate_y);
	for (i = 0; i < 17; i++) {
		float arch_cos = (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f/2.8f)*cosf(i * ((float)M_PI) / 16.0f);
		float arch_sin = (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f/2.8f)*sinf(i * ((float)M_PI) / 16.0f);
		a_points[i+1]  = imvec2_cmac(centre_x, centre_y, pre_offset_x + arch_cos, ay + arch_sin, scale_rotate_x, scale_rotate_y);
	}
	a_points[i+1] = imvec2_cmac(centre_x, centre_y, pre_offset_x - (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f/2.8f), -HEXAGON_CENTRE_TO_EDGE_CENTRE_DISTANCE+HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.25f, scale_rotate_x, scale_rotate_y);
	p_list->AddConvexPolyFilled(a_points, 19, colour);
	a_points[0] = imvec2_cmac(centre_x, centre_y, pre_offset_x,      by - tr,      scale_rotate_x, scale_rotate_y);
	a_points[1] = imvec2_cmac(centre_x, centre_y, pre_offset_x + tr, by + (SQRT3 - 1)*tr, scale_rotate_x, scale_rotate_y);
	a_points[2] = imvec2_cmac(centre_x, centre_y, pre_offset_x - tr, by + (SQRT3 - 1)*tr, scale_rotate_x, scale_rotate_y);
	p_list->AddConvexPolyFilled(a_points, 3, colour);
	a_points[0] = imvec2_cmac(centre_x, centre_y, pre_offset_x + (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f/2.8f), by, scale_rotate_x, scale_rotate_y);
	a_points[1] = imvec2_cmac(centre_x, centre_y, pre_offset_x + (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f/2.8f), -HEXAGON_CENTRE_TO_EDGE_CENTRE_DISTANCE-HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.25f, scale_rotate_x, scale_rotate_y);
	a_points[2] = imvec2_cmac(centre_x, centre_y, pre_offset_x - (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f/2.8f), -HEXAGON_CENTRE_TO_EDGE_CENTRE_DISTANCE-HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.25f, scale_rotate_x, scale_rotate_y);
	a_points[3] = imvec2_cmac(centre_x, centre_y, pre_offset_x - (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f/2.8f), by, scale_rotate_x, scale_rotate_y);
	p_list->AddConvexPolyFilled(a_points, 4, colour);
}


static
void
draw_inverted_arrow(float centre_x, float centre_y, float pre_offset_x, float scale_rotate_x, float scale_rotate_y, ImU32 colour) {
	float       ay = -(HEXAGON_CENTRE_TO_EDGE_CENTRE_DISTANCE - HEXAGON_INNER_TO_OUTER_EDGE_PARALLEL_DISTANCE);
	float       by = -(HEXAGON_CENTRE_TO_EDGE_CENTRE_DISTANCE + HEXAGON_INNER_TO_OUTER_EDGE_PARALLEL_DISTANCE);
	float       tr = (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE/2.8f);
	ImVec2      a_points[19];
	int         i;
	ImDrawList *p_list = ImGui::GetWindowDrawList();
	a_points[0] = imvec2_cmac(centre_x, centre_y, pre_offset_x + (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f/2.8f), by, scale_rotate_x, scale_rotate_y);
	for (i = 0; i < 17; i++) {
		float arch_cos = (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f/2.8f)*cosf(i * ((float)M_PI) / 16.0f);
		float arch_sin = (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f/2.8f)*sinf(i * ((float)M_PI) / 16.0f);
		a_points[i+1]  = imvec2_cmac(centre_x, centre_y, pre_offset_x + arch_cos, ay + arch_sin, scale_rotate_x, scale_rotate_y);
	}
	a_points[i+1] = imvec2_cmac(centre_x, centre_y, pre_offset_x - (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f/2.8f), by, scale_rotate_x, scale_rotate_y);
	p_list->AddConvexPolyFilled(a_points, 19, colour);
	a_points[0] = imvec2_cmac(centre_x, centre_y, pre_offset_x,      by - tr,      scale_rotate_x, scale_rotate_y);
	a_points[1] = imvec2_cmac(centre_x, centre_y, pre_offset_x + tr, by + (SQRT3 - 1)*tr, scale_rotate_x, scale_rotate_y);
	a_points[2] = imvec2_cmac(centre_x, centre_y, pre_offset_x - tr, by + (SQRT3 - 1)*tr, scale_rotate_x, scale_rotate_y);
	p_list->AddConvexPolyFilled(a_points, 3, colour);
}


void plot_grid(struct gridstate *p_st, struct plot_grid_state *p_state, struct program *p_prog)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

	ImVec2 graph_size;
	float radius = p_state->radius;
	graph_size.x = ImGui::CalcItemWidth();
	graph_size.y = style.FramePadding.y*2 + 700;

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

	struct visible_cell_iterator    iter;
	const struct visible_cell_info *p_info;
	struct layer_edge_iterator      edge_iter;
	const struct layer_edge_info   *p_edge_info;

	int b_real_click_occured = 0;

	uint64_t glfw_ticks = glfwGetTimerValue();
	uint64_t glfw_ticks_per_sec = glfwGetTimerFrequency();

	uint32_t animation_frame = ((glfw_ticks % glfw_ticks_per_sec) * 256 / glfw_ticks_per_sec);
	float    animation_frame_sin = sinf(animation_frame * (float)(2.0*M_PI/256));
	float    animation_frame_cos = cosf(animation_frame * (float)(2.0*M_PI/256));

	int detail_alpha   = (int)((radius - 6.0f)*25.0f); /* at 16 it's fully visible, at 6 it's fully gone */
	int overview_alpha = (int)((12 - radius)*43.0f);    /* at 6 it's fully visible, at 12 it's fully gone */
	if (detail_alpha < 0)
		detail_alpha = 0;
	if (detail_alpha > 255)
		detail_alpha = 255;
	if (overview_alpha < 0)
		overview_alpha = 0;
	if (overview_alpha > 255)
		overview_alpha = 255;

	/* Figure out if the mouse has come down over this component. If so,
	 * record the down position and clear the position-changed flag. We use
	 * this to detect if we have been dragging the grid around to avoid
	 * clicking on a control. */
	if (hovered && g.IO.MouseDown[0] && !p_state->mouse_down) {
		p_state->mouse_down_pos           = mprel;
		p_state->mouse_down               = 1;
		p_state->b_mouse_down_pos_changed = 0;

		/* todo: I can't remember what these are for at all. */
		ImGui::SetActiveID(id, window);
		ImGui::SetFocusID(id, window);
		ImGui::FocusWindow(window);
	}

	/* If the mouse is down, implement dragging the grid around. */
	if (p_state->mouse_down) {
		ImVec2 drag = iv2_sub(mprel, p_state->mouse_down_pos);

		if (drag.x != 0.0f || drag.y != 0.0f) {
			p_state->b_mouse_down_pos_changed = 1;
		}

		bl_x -= (int64_t)(drag.x/(radius*3.0f)*65536.0f);
		bl_y -= (int64_t)(drag.y/(radius*0.866f)*65536.0f);

		if (bl_x < 0)
			bl_x = 0;
		if (bl_y < 0)
			bl_y = 0;

		if (!g.IO.MouseDown[0]) {
			ImGui::ClearActiveID();
			p_state->mouse_down = 0;
			p_state->bl_x = bl_x;
			p_state->bl_y = bl_y;

			b_real_click_occured = !p_state->b_mouse_down_pos_changed;
		}
	}

	if (hovered) {
		if (io.MouseWheel != 0.0f) {
			bl_x -= (int64_t)(-mprel.x/(radius*3.0f)*65536.0f);
			bl_y -= (int64_t)(-mprel.y/(radius*0.866f)*65536.0f);

			radius *= powf(2.0, io.MouseWheel/40.0f);
			if (radius < 3.0f)
				radius = 3.0f;
			p_state->radius = radius;

			bl_x -= (int64_t)(+mprel.x/(radius*3.0f)*65536.0f);
			bl_y -= (int64_t)(+mprel.y/(radius*0.866f)*65536.0f);

			if (bl_x < 0)
				bl_x = 0;
			if (bl_y < 0)
				bl_y = 0;

			p_state->bl_x = bl_x;
			p_state->bl_y = bl_y;
		}
	}

	/* Handle edge clicks. */
	layer_edge_iterator_init(&edge_iter, bl_x, bl_y, inner_bb, radius, io.MousePos);
	while ((p_edge_info = layer_edge_iterator_next(&edge_iter)) != NULL) {
		if (b_real_click_occured && p_edge_info->b_mouse_over_either) {
			const struct gridaddr *p_addr = (p_edge_info->b_mouse_in_addr) ? &(p_edge_info->addr) : &(p_edge_info->addr_other);
			int                    edge   = (p_edge_info->b_mouse_in_addr) ? p_edge_info->edge : get_opposing_edge_id(p_edge_info->edge);
			struct gridcell       *p_cell;
			if ((p_cell = gridstate_get_gridcell(p_st, p_addr, 1)) != NULL) {
				if (!gridcell_set_neighbour_edge_connection_type(p_cell, edge, get_next_connection_type(gridcell_get_neighbour_edge_connection_type(p_cell, edge)))) {
					program_compile(p_prog, p_st);
				}
			}
		}
	}

	/* Handle centre clicks and double clicks. */
	visible_cell_iterator_init(&iter, bl_x, bl_y, inner_bb, radius, io.MousePos);
	while ((p_info = visible_cell_iterator_next(&iter)) != NULL) {
		if (p_info->b_cursor_in_cell && b_real_click_occured) {
			if (g.IO.KeyShift) {
				/* Clear all cell connections. */
				struct gridcell *p_cell_l0 = gridstate_get_gridcell(p_st, &(p_info->addr_l0), 0);
				if (p_cell_l0 != NULL) {
					int i, j;
					for (j = 0; j < NUM_LAYERS; j++) {
						for (i = 0; i < EDGE_DIR_NUM; i++) {
							if (gridcell_get_edge_neighbour(p_cell_l0 + 256*j, i, 0) != NULL) {
								gridcell_set_neighbour_edge_connection_type(p_cell_l0 + 256*j, i, EDGE_LAYER_CONNECTION_UNCONNECTED);
							}
						}
					}
					/* todo - only do this if something changed. */
					program_compile(p_prog, p_st);
				}
			} else {
				/* Toggle layer fusing. */
				float x = p_info->cursor_relative_to_centre_x;
				float y = p_info->cursor_relative_to_centre_y;
				if (x*x + y*y < 0.433*0.433) {
					struct gridcell *p_cell = gridstate_get_gridcell(p_st, &(p_info->addr_l0), 1);
					if (p_cell != NULL) {
						gridcell_are_layers_fused_toggle(p_cell);
						program_compile(p_prog, p_st);
					}
				}
			}
		}
	}

	if (program_is_valid(p_prog)) {
		program_run(p_prog);
	}

    ImGui::RenderFrame(frame_bb.Min, frame_bb.Max, ImColor(128, 128, 128, 255), true, style.FrameRounding);

	ImDrawList *p_list = ImGui::GetWindowDrawList();

    p_list->PushClipRect(inner_bb.Min, inner_bb.Max, true);  // Render-level scissoring. This is passed down to your render function but not used for CPU-side coarse clipping. Prefer using higher-level ImGui::PushClipRect() to affect logic (hit-testing and widget culling)

	/* 1) Draw hexagons */
	visible_cell_iterator_init(&iter, bl_x, bl_y, inner_bb, radius, io.MousePos);
	while ((p_info = visible_cell_iterator_next(&iter)) != NULL) {
		float            inner_radius = radius * 0.95f;
		struct gridcell *p_cell_l0 = gridstate_get_gridcell(p_st, &(p_info->addr_l0), 0);
		ImVec2           a_points[6];

		a_points[0] = ImVec2(p_info->centre_pixel_x - inner_radius,      p_info->centre_pixel_y);
		a_points[1] = ImVec2(p_info->centre_pixel_x - 0.5f*inner_radius, p_info->centre_pixel_y - SQRT3_4*inner_radius);
		a_points[2] = ImVec2(p_info->centre_pixel_x + 0.5f*inner_radius, p_info->centre_pixel_y - SQRT3_4*inner_radius);
		a_points[3] = ImVec2(p_info->centre_pixel_x + inner_radius,      p_info->centre_pixel_y);
		a_points[4] = ImVec2(p_info->centre_pixel_x + 0.5f*inner_radius, p_info->centre_pixel_y + SQRT3_4*inner_radius);
		a_points[5] = ImVec2(p_info->centre_pixel_x - 0.5f*inner_radius, p_info->centre_pixel_y + SQRT3_4*inner_radius);
		p_list->AddConvexPolyFilled(a_points, 6, ImColor(255, 255, 255, 255));

		if (p_cell_l0 != NULL && !program_is_valid(p_prog)) {
			int i;
			int sp;
			for (i = 0, sp = 0; i < NUM_LAYERS; i++) {
				int j;
				if (p_cell_l0[i*256].data & GRIDCELL_PROGRAM_BUSY_BIT) {
					float sx, sy;
					for (j = 0; j < 4; j++) {
						sx = cosf((j+6*sp)*(float)(2*M_PI/18))*0.533f*radius;
						sy = sinf((j+6*sp)*(float)(2*M_PI/18))*0.533f*radius;
						p_list->AddCircleFilled
							(imvec2_cmac(p_info->centre_pixel_x, p_info->centre_pixel_y, sx, sy, animation_frame_cos, animation_frame_sin)
							,radius*0.09f
							,ImColor
								(192 - ((i==1 || i==2) ? 64 : 0)
								,192 - ((i==0 || i==2) ? 64 : 0)
								,192 - ((i==0 || i==1) ? 64 : 0)
								,detail_alpha
								)
							,17
							);
					}
					sp++;
				}
			}
		}
	}

	/* 2) Draw edges */
	if (detail_alpha > 0) {
		layer_edge_iterator_init(&edge_iter, bl_x, bl_y, inner_bb, radius, io.MousePos);
		while ((p_edge_info = layer_edge_iterator_next(&edge_iter)) != NULL) {
			struct gridcell *p_cell      = gridstate_get_gridcell(p_st, &(p_edge_info->addr), 0);
			struct gridcell *p_neighbour = gridstate_get_gridcell(p_st, &(p_edge_info->addr_other), 0);
			if (p_cell && p_neighbour) {
				int              ctype         = gridcell_get_edge_connection_type(p_cell, p_edge_info->edge);
				int              ntype         = gridcell_get_edge_connection_type(p_neighbour, get_opposing_edge_id(p_edge_info->edge));
				int              b_busted_edge = ((p_cell->data & GRIDCELL_PROGRAM_BROKEN_BIT_MASK(p_edge_info->edge)) || (p_neighbour->data & GRIDCELL_PROGRAM_BROKEN_BIT_MASK(get_opposing_edge_id(p_edge_info->edge))));
				float            ax            = AA_INNER_EDGE_CENTRE_POINTS[p_edge_info->layer][0];
				float            ay            = AA_INNER_EDGE_CENTRE_POINTS[p_edge_info->layer][1];
				float            bx            = ax;
				float            by            = ay - HEXAGON_INNER_TO_OUTER_EDGE_PARALLEL_DISTANCE*2;
				float            rx            = AA_EDGE_ROTATORS[p_edge_info->edge][0]*radius;
				float            ry            = AA_EDGE_ROTATORS[p_edge_info->edge][1]*radius;

				float busted_selected_intensity = 255 - (b_busted_edge ? (int)(0.5f + (animation_frame_sin + 1.0)*48.0f) : 0);
				int   selected_intensity        = b_busted_edge ? busted_selected_intensity : 192;
				int   unselected_intensity      = b_busted_edge ? 96                        : 128;

				ImU32 layer_colour = ImColor
					((p_edge_info->layer == 0) ? selected_intensity : unselected_intensity
					,(p_edge_info->layer == 1) ? selected_intensity : unselected_intensity
					,(p_edge_info->layer == 2) ? selected_intensity : unselected_intensity
					,detail_alpha
					);

				if (ctype == EDGE_LAYER_CONNECTION_NET_CONNECTED) {
					ImVec2 a_points[34];
					int i;
					for (i = 0; i < 17; i++) {
						float arch_cos = (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE/2.8f)*cosf(i * ((float)M_PI) / 16.0f);
						float arch_sin = (HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE/2.8f)*sinf(i * ((float)M_PI) / 16.0f);
						a_points[i]    = imvec2_cmac(p_edge_info->centre_pixel_x, p_edge_info->centre_pixel_y, ax + arch_cos, ay + arch_sin, rx, ry);
						a_points[i+17] = imvec2_cmac(p_edge_info->centre_pixel_x, p_edge_info->centre_pixel_y, bx - arch_cos, by - arch_sin, rx, ry);
					}
					p_list->AddConvexPolyFilled(a_points, 34, layer_colour);
				} else if (ctype == EDGE_LAYER_CONNECTION_RECEIVES_DELAY_INVERTED) {
					assert(ntype == EDGE_LAYER_CONNECTION_SENDS);
					draw_inverted_delayed_arrow
						(p_edge_info->centre_pixel_x + radius*AA_NEIGHBOUR_CENTRE_OFFSETS[p_edge_info->edge][0]
						,p_edge_info->centre_pixel_y + radius*AA_NEIGHBOUR_CENTRE_OFFSETS[p_edge_info->edge][1]
						,AA_INNER_EDGE_CENTRE_POINTS[2-p_edge_info->layer][0]
						,-rx
						,-ry
						,layer_colour
						);
				} else if (ctype == EDGE_LAYER_CONNECTION_RECEIVES_INVERTED) {
					assert(ntype == EDGE_LAYER_CONNECTION_SENDS);
					draw_inverted_arrow
						(p_edge_info->centre_pixel_x + radius*AA_NEIGHBOUR_CENTRE_OFFSETS[p_edge_info->edge][0]
						,p_edge_info->centre_pixel_y + radius*AA_NEIGHBOUR_CENTRE_OFFSETS[p_edge_info->edge][1]
						,AA_INNER_EDGE_CENTRE_POINTS[2-p_edge_info->layer][0]
						,-rx
						,-ry
						,layer_colour
						);
				} else if (ntype == EDGE_LAYER_CONNECTION_RECEIVES_DELAY_INVERTED) {
					assert(ctype == EDGE_LAYER_CONNECTION_SENDS);
					draw_inverted_delayed_arrow
						(p_edge_info->centre_pixel_x
						,p_edge_info->centre_pixel_y
						,AA_INNER_EDGE_CENTRE_POINTS[p_edge_info->layer][0]
						,rx
						,ry
						,layer_colour
						);
				} else if (ntype == EDGE_LAYER_CONNECTION_RECEIVES_INVERTED) {
					assert(ctype == EDGE_LAYER_CONNECTION_SENDS);
					draw_inverted_arrow
						(p_edge_info->centre_pixel_x
						,p_edge_info->centre_pixel_y
						,AA_INNER_EDGE_CENTRE_POINTS[p_edge_info->layer][0]
						,rx
						,ry
						,layer_colour
						);
				} else {
					assert(ntype == EDGE_LAYER_CONNECTION_UNCONNECTED);
					assert(ctype == EDGE_LAYER_CONNECTION_UNCONNECTED);
				}
			}
		}
	}

	/* 3) Draw status and cursor over stuff */
	layer_edge_iterator_init(&edge_iter, bl_x, bl_y, inner_bb, radius, io.MousePos);
	while ((p_edge_info = layer_edge_iterator_next(&edge_iter)) != NULL) {
		/* Render edge connector hovers */
		if (p_edge_info->b_mouse_over_either && detail_alpha > 0) {
			float ax = AA_INNER_EDGE_CENTRE_POINTS[p_edge_info->layer][0];
			float ay = AA_INNER_EDGE_CENTRE_POINTS[p_edge_info->layer][1]; /* small negative numbers */
			float bx = ax;
			float by = ay - HEXAGON_INNER_TO_OUTER_EDGE_PARALLEL_DISTANCE*2;
			float rx = AA_EDGE_ROTATORS[p_edge_info->edge][0]*radius;
			float ry = AA_EDGE_ROTATORS[p_edge_info->edge][1]*radius;
			ImU32 layer_colour = ImColor
				(((p_edge_info->layer == 0) ? 150 : 100)
				,((p_edge_info->layer == 1) ? 150 : 100)
				,((p_edge_info->layer == 2) ? 150 : 100)
				,(detail_alpha*64)/256
				);
			int i;
			ImVec2 a_points[34];
			for (i = 0; i < 17; i++) {
				float arch_cos = HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f*cosf(i * ((float)M_PI) / 16.0f);
				float arch_sin = HEXAGON_INNER_CENTRE_EDGE_TO_OTHER_LAYER_CENTRE_DISTANCE*0.5f*sinf(i * ((float)M_PI) / 16.0f);
				a_points[i]    = imvec2_cmac(p_edge_info->centre_pixel_x, p_edge_info->centre_pixel_y, ax + arch_cos, ay + arch_sin, rx, ry);
				a_points[i+17] = imvec2_cmac(p_edge_info->centre_pixel_x, p_edge_info->centre_pixel_y, bx - arch_cos, by - arch_sin, rx, ry);
			}
			p_list->AddConvexPolyFilled(a_points, 34, layer_colour);
		}

		if (p_edge_info->edge == 0) {
			struct gridcell *p_cell = gridstate_get_gridcell(p_st, &(p_edge_info->addr), 0);

			/* Render active states. */
			if (p_cell != NULL && detail_alpha > 0 && CELL_WILL_GET_A_NET(p_cell->data) && program_is_valid(p_prog)) {
				uint32_t net_id   = GRIDCELL_PROGRAM_NET_ID_BITS_GET(p_cell->data);
				int      b_active = (p_prog->p_last_data[net_id>>6] & BIT_MASKS[net_id & 0x3F]) != 0;

				ImU32 layer_colour = ImColor
					(((p_edge_info->layer == 0) ? 224-32*b_active : 196-96*b_active)
					,((p_edge_info->layer == 1) ? 224-32*b_active : 196-96*b_active)
					,((p_edge_info->layer == 2) ? 224-32*b_active : 196-96*b_active)
					,detail_alpha
					);
				float px = p_edge_info->centre_pixel_x - sinf(p_edge_info->layer*(float)(2*M_PI/3))*radius*(3*0.433f/4);
				float py = p_edge_info->centre_pixel_y - cosf(p_edge_info->layer*(float)(2*M_PI/3))*radius*(3*0.433f/4);
				p_list->AddCircleFilled(ImVec2(px, py), radius*0.07, layer_colour, 17);
			}

			if (p_edge_info->layer == 0 && detail_alpha > 0) {
				/* Render the layer fusing pad. */
				if (p_cell != NULL && gridcell_are_layers_fused_get(p_cell)) {
					p_list->AddCircleFilled(ImVec2(p_edge_info->centre_pixel_x, p_edge_info->centre_pixel_y), radius*0.2, ImColor(128, 128, 128, detail_alpha), 17);
				}

				/* Render the hover over the layer fusing pad. */
				if (p_edge_info->b_mouse_in_addr) {
					float dx = p_edge_info->cursor_relative_to_centre_x;
					float dy = p_edge_info->cursor_relative_to_centre_y;
					if (dx*dx + dy*dy < 0.433f*0.433f)
						p_list->AddCircleFilled(ImVec2(p_edge_info->centre_pixel_x, p_edge_info->centre_pixel_y), radius*0.433f, ImColor(160, 160, 160, (detail_alpha*64)/256), 17);
				}
			}
		}

		/* only do this on the very last edge and layer of this address. We're
		 * pooing pixels on top of everything. */
		if (overview_alpha > 0 && p_edge_info->edge == 2 && p_edge_info->layer == 2) {
			struct gridcell *p_cell = gridstate_get_gridcell(p_st, &(p_edge_info->addr), 0);
			if (p_cell != NULL) {
				int i;
				int b_busted;
				int b_interesting;

				for (i = 0; i < NUM_LAYERS && ((p_cell - i*256)->data & GRIDCELL_PROGRAM_BUSY_BIT) == 0; i++);
				b_busted      = (i != NUM_LAYERS);

				for (i = 0; i < NUM_LAYERS && CELL_WILL_GET_A_NET((p_cell - i*256)->data) == 0; i++);
				b_interesting = (i != NUM_LAYERS);

				if (b_interesting || b_busted) {
					float            inner_radius = radius * 0.95f;
					ImVec2           a_points[6];
					a_points[0] = ImVec2(p_edge_info->centre_pixel_x - inner_radius,      p_edge_info->centre_pixel_y);
					a_points[1] = ImVec2(p_edge_info->centre_pixel_x - 0.5f*inner_radius, p_edge_info->centre_pixel_y - SQRT3_4*inner_radius);
					a_points[2] = ImVec2(p_edge_info->centre_pixel_x + 0.5f*inner_radius, p_edge_info->centre_pixel_y - SQRT3_4*inner_radius);
					a_points[3] = ImVec2(p_edge_info->centre_pixel_x + inner_radius,      p_edge_info->centre_pixel_y);
					a_points[4] = ImVec2(p_edge_info->centre_pixel_x + 0.5f*inner_radius, p_edge_info->centre_pixel_y + SQRT3_4*inner_radius);
					a_points[5] = ImVec2(p_edge_info->centre_pixel_x - 0.5f*inner_radius, p_edge_info->centre_pixel_y + SQRT3_4*inner_radius);
					p_list->AddConvexPolyFilled(a_points, 6, b_busted ? ImColor(192, 64, 64, overview_alpha) : ImColor(160, 160, 160, overview_alpha));
				}
			}
		}
	}




#if 0
			draw_edge_arrows(edge_mode_n, p.x, p.y, 0.0f, -0.866f, radius);
			draw_edge_arrows(edge_mode_ne, p.x, p.y, 0.75f, -0.433f, radius);
			draw_edge_arrows(edge_mode_se, p.x, p.y, 0.75f, +0.433f, radius);
#endif

	p_list->PopClipRect();

#if 1
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize()*35.0f);

		ImGui::Text("animation frame=%d", animation_frame);

#if 0
		ImGui::Text
			("down=%d (lda=%f,%f) snap=(%ld,%ld) cell=(%f,%f) errors=%d"
			,p_state->mouse_down
			,mprel.x
			,mprel.y
			,(long)((int32_t)p_hc->addr.x) - 0x40000000
			,(long)((int32_t)p_hc->addr.y) - 0x40000000
			,p_hc->pos_in_cell.x
			,p_hc->pos_in_cell.y
			,errors
			);
#endif
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
#endif
}




int main(int argc, char **argv)
{

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
		if (gridaddr_edge_neighbour(&pos, &pos, dir_get_opposing(directions[(i*13)%100])))
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
		int edge_dir = (pdir = (pdir*16 + 112*(rand()%EDGE_DIR_NUM) + 64)/128);
		int virt_dir = rand()%VERTEX_DIR_NUM;
		int virt_ctl = rand() & 1;

		if ((p_neighbour = gridcell_get_vertex_neighbour(p_cell, virt_dir, 1)) == NULL)
			abort();
		gridcell_set_vert_flags_adv(p_cell, p_neighbour, virt_dir, virt_ctl);

		if ((p_neighbour = gridcell_get_edge_neighbour(p_cell, edge_dir, 1)) == NULL)
			abort();
		
		switch (rand()%5) {
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
#elif 0
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
	plot_state.bl_x = 0x40000000ull*65536;
	plot_state.bl_y = 0x40000000ull*65536;
	plot_state.mouse_down = 0;

#if 0
	/* build snek - todo: do it better
	 *
	 * three snakes could be active at any given time.
	 * each one should be maybe 80 degrees? idk - knob twiddle till it looks good, right?
	 * 
	 * UPDATE: was a nice idea but i can't figure out how to make this thing
	 * get filled. it is not convex. idek... */
	{
		/* figure out a good arc length */
		float angle_rads = (float)(2*M_PI/4);
		float r_short = 0.45f;

		/* derive r2 such that the snake lies on the radius 1.0f */
		float r_long = 0.65f;

		/* radius of each end disc */
		float r_end_disc = (r_long - r_short)*0.5f;

		/* radius of the centre ring */
		float r_centre = (r_long + r_short)*0.5f;

		/* compute the various parts of circumference */
		float short_arc_segment_length = r_short*angle_rads;
		float long_arc_segment_length  = r_long*angle_rads;
		float end_segment_length       = ((float)M_PI)*r_end_disc;

		/* find the total circumference. */
		float total_circumference = 2.0f*end_segment_length + short_arc_segment_length + long_arc_segment_length;

		/* point allocation for each segment */
		int points_per_end_segment  = (int)(NUM_SNAKE_POINTS*end_segment_length/total_circumference + 0.5f);
		int points_on_short_segment = (int)(NUM_SNAKE_POINTS*short_arc_segment_length/total_circumference + 0.5f);
		int points_on_long_segment  = NUM_SNAKE_POINTS - 2*points_per_end_segment - points_on_short_segment;

		/* find end position */
		float  rx   = cosf(angle_rads);
		float  ry   = sinf(angle_rads);
		float *p_dp = &(plot_state.aa_the_snake[0][0]);

		int i;

		/* build long segments starting at 1.0, 0.0 */
		for (i = 0; i < points_on_long_segment; i++) {
			*p_dp++ = r_long*cosf(i*angle_rads/points_on_long_segment);
			*p_dp++ = r_long*sinf(i*angle_rads/points_on_long_segment);
		}

		/* build first cap */
		for (i = 0; i < points_per_end_segment; i++) {
			float dx = r_centre + r_end_disc*cosf(i*((float)M_PI)/points_per_end_segment);
			float dy =            r_end_disc*sinf(i*((float)M_PI)/points_per_end_segment);
			vmpy(&dx, &dy, rx, ry);
			*p_dp++ = dx;
			*p_dp++ = dy;
		}

		/* build short segment */
		for (i = 0; i < points_on_short_segment; i++) {
			*p_dp++ = r_short*cosf((points_on_short_segment-1-i)*angle_rads/points_on_short_segment);
			*p_dp++ = r_short*sinf((points_on_short_segment-1-i)*angle_rads/points_on_short_segment);
		}

		/* build last cap */
		for (i = 0; i < points_per_end_segment; i++) {
			float dx = r_centre - r_end_disc*cosf(i*((float)M_PI)/points_per_end_segment);
			float dy =           -r_end_disc*sinf(i*((float)M_PI)/points_per_end_segment);
			*p_dp++ = dx;
			*p_dp++ = dy;
		}
	}
#endif

	struct program prog;
	program_init(&prog);

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

			ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f/ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
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
		plot_grid(&gs, &plot_state, &prog);
		if (gs.stats_ok) {
			int64_t width = gs.stats.max_x - (int64_t)gs.stats.min_x + 1;
			int64_t height = gs.stats.max_y - (int64_t)gs.stats.min_y + 1;
			ImGui::SameLine();
			ImGui::BeginGroup();
			ImGui::Text("Grid Width: %lld", (long long)width);
			ImGui::Text("Grid Height: %lld", (long long)height);
			ImGui::Text("Grid Area: %lld", (long long)width*height);
			ImGui::Text("Total Cells: %ld", (long)gs.stats.num_cells);
			ImGui::Text("Area Efficiency %f", gs.stats.num_cells*100.0f/(float)(width*height));
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
		glClearColor(clear_color.x*clear_color.w, clear_color.y*clear_color.w, clear_color.z*clear_color.w, clear_color.w);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		glfwSwapBuffers(window);
	}

#if 0
	if (argc > 1) {
		if (grid_save(&gs, argv[1])) {
			printf("failed to save grid to %s\n", argv[1]);
		} else {
			printf("saved grid to %s\n", argv[1]);
		}
	}
#endif

	// Cleanup
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}





#if 0

static int grid_save_rec(FILE *p_f, struct gridpage *p_page) {
	int i;

	for (i = 0; i < PAGE_XY_NB*PAGE_XY_NB; i++) {
		uint64_t storage_flags;
		struct gridaddr addr;

		if ((storage_flags = p_page->data[i].flags & STORAGE_FLAG_MASK) == 0)
			continue;

		addr.x = p_page->position.x | (i & PAGE_XY_MASK);
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

#endif


