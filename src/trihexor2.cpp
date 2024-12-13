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


/* tests if any cell edges are not disconnected, the layers are merged or the FORCE_NET bit is set. */
#define CELL_DESERVES_A_NET(data_)           (((data_) & (GRIDCELL_BIT_MERGED_LAYERS | GRIDCELL_BIT_FORCE_NET | 0xFFFFC00)) != 0)


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
	 * 33    - COMPUTING
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

/* Before running this, the caller can initialise any field input bits to
 * 1 in the p_data pointer. p_data is zeroed after every execution so
 * externally supplied bits must be written prior to calling program_run
 * every time. Also, neither p_data nor p_last_data can be cached, their
 * values will change after every call to program_run.
 * 
 * After running this, output data for each net is in p_last_data. */
static void program_run(struct program *p_program) {
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

	/* run the program. */
	size_t    dest_net;
	uint32_t *p_code = p_program->p_code;
	for (dest_net = 0; dest_net < p_program->net_count; dest_net++) {
		size_t    nb_sources = *p_code++;
		uint64_t *p_dest     = &(p_program->p_data[dest_net >> 6]);
		uint64_t  dest_bit   = BIT_MASKS[dest_net & 0x3F];
		uint64_t  dest_val   = *p_dest;
		while (/* early exit condition */ (dest_val & dest_bit) == 0 && nb_sources--) {
			uint32_t  op         = *p_code++;
			uint32_t  src_net    = *p_code++;
			uint64_t *p_data_src = (op & 0x1) ? p_program->p_last_data : p_program->p_data;
			uint64_t  src_data   = (assert((op & 0x1) != 0 || (src_net < dest_net)), p_data_src[src_net >> 6]);
			uint64_t  src_val    = src_data; /* (op & 0x2) ? ~src_data : src_data; <<<<< MAYBE - DO WE WANT DIODES? */
			uint64_t  src_ctl    = src_val & BIT_MASKS[src_net & 0x3F];
			dest_val |= (src_ctl) ? 0 : dest_bit;
			assert(op < 2);
		}
		*p_dest = dest_val;
		p_code += 2*nb_sources;
	}

	/* pointer jiggle and state reset. */
	{
		uint64_t *p_old_data   = p_program->p_data;
		p_program->p_data      = p_program->p_last_data;
		p_program->p_last_data = p_old_data;
	}

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

static uint32_t *program_code_reserve(struct program *p_program, size_t n) {
	p_program->code_count += n;
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
	return &(p_program->p_code[p_program->code_count - n]);
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
				if (!CELL_DESERVES_A_NET(p_gp->data[j].data))
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
	 * execution. We clear the busy flag for no real reason. */
	{
		size_t i;
		for (i = 0; i < p_program->net_count; i++) {
			struct program_net *p_net = p_program->pp_netstack[p_program->net_count-1-i];
			uint32_t j;
			uint64_t new_id_mask = GRIDCELL_PROGRAM_NET_ID_BITS_SET(i);
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
			uint32_t *p_code_start = program_code_reserve(p_program, 1);
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
						uint32_t        *p_data              = program_code_reserve(p_program, 2);
						assert(gridcell_get_edge_connection_type(p_neighbour, get_opposing_edge_id(k)) == EDGE_LAYER_CONNECTION_SENDS);
						if (edge_type == EDGE_LAYER_CONNECTION_RECEIVES_INVERTED) {
							assert(neighbour_net < i);
							p_data[0] = 0;
							p_data[1] = neighbour_net;
						} else {
							assert(edge_type == EDGE_LAYER_CONNECTION_RECEIVES_DELAY_INVERTED);
							assert(neighbour_net < p_program->net_count);
							p_data[0] = 1;
							p_data[1] = neighbour_net;
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
					uint32_t  op         = *p_code++;
					uint32_t  src_net    = *p_code++;
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

#define EDGE_NOTHING      (0)


#include "imgui_internal.h"

#define ASR(n_, b_) ((n_) >> (b_))

#define SQRT3   (1.732050807568877f)
#define SQRT3_4 (SQRT3*0.5f)

int get_cursor_hex_addr(struct gridaddr *p_addr, int64_t bl_x, int64_t bl_y, float cursor_x, float cursor_y, ImVec2 *prcc) {
	int64_t thx     = bl_x + (int32_t)(cursor_x*(65536.0f/3.0f));
	int64_t thy     = bl_y + (int32_t)(cursor_y*(65536.0f/SQRT3_4));

	int64_t wholex  = ASR(thx, 16);
	int64_t wholey  = ASR(thy, 17)*2;

	float   fbl_x   = ((float)(int32_t)(((uint32_t)thx) & 0xFFFF))*(3.0f/65536.0f);
	float   fbl_y   = ((float)(int32_t)(((uint32_t)thy) & 0x1FFFF))*(SQRT3_4/65536.0f);

	float y_dbot    = fbl_y;
	float y_dmid    = fbl_y - SQRT3_4;
	float y_dtop    = fbl_y - 2.0f*SQRT3_4;
	float x_dleft   = fbl_x;
	float x_dmid    = fbl_x - 1.5f;
	float x_dright  = fbl_x - 3.0f;

	float y_dbot2   = y_dbot*y_dbot;
	float y_dmid2   = y_dmid*y_dmid;
	float y_dtop2   = y_dtop*y_dtop;
	float x_dleft2  = x_dleft*x_dleft;
	float x_dmid2   = x_dmid*x_dmid;
	float x_dright2 = x_dright*x_dright;

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

#define ADDR_XY_VALID(x_) ((x_) >= 0 && (x_) < (int32_t)0x7FFFFFFF)

	if (!ADDR_XY_VALID(out_x) || !ADDR_XY_VALID(out_y))
		return 1;

	prcc->x   = fbl_x;
	prcc->y   = fbl_y;
	p_addr->x = (uint32_t)out_x;
	p_addr->y = (uint32_t)out_y;
	p_addr->z = 0;

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

	int layer_idx;
	int edge_idx;
	
};

static void vmpy(float *p_x, float *p_y, float x2, float y2) {
	float x1 = *p_x;
	float y1 = *p_y;
	*p_x = x1*x2 - y1*y2;
	*p_y = x1*y2 + y1*x2;
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

	struct highlighted_cell hc;
	struct highlighted_cell *p_hc = NULL;

	int errors = 0;

	if (!get_cursor_hex_addr(&(hc.addr), bl_x, bl_y, mprel.x/radius,  mprel.y/radius, &(hc.pos_in_cell))) {
		float rat = hc.pos_in_cell.y/hc.pos_in_cell.x;
		hc.edge_idx = EDGE_DIR_N;
		hc.layer_idx = 0;


#if 0
		0.36397023 /* 20 */
		0.83909963 /* 40 */
		1.73205081 /* 60 */
		5.67128182 /* 80 */
#endif

		if (hc.pos_in_cell.x > 0) {
			if (rat < -5.671f) {
				hc.edge_idx  = EDGE_DIR_S;    /* 0.5,-0.866 -> -0.5, 0.866 */
				hc.layer_idx = 1;
			} else if (rat < -1.732f) {
				hc.edge_idx  = EDGE_DIR_S;    /* 0.5,-0.866 -> -0.5, 0.866 */
				hc.layer_idx = 2;
			} else if (rat < -0.839f) {
				hc.edge_idx  = EDGE_DIR_SE;
				hc.layer_idx = 0;
			} else if (rat < -0.364f) {
				hc.edge_idx  = EDGE_DIR_SE;
				hc.layer_idx = 1;
			} else if (rat < 0.0f) {
				hc.edge_idx  = EDGE_DIR_SE;
				hc.layer_idx = 2;
			} else if (rat < 0.364f) {
				hc.edge_idx = EDGE_DIR_NE;
				hc.layer_idx = 0;
			} else if (rat < 0.839f) {
				hc.edge_idx = EDGE_DIR_NE;
				hc.layer_idx = 1;
			} else if (rat < 1.732f) {
				hc.edge_idx = EDGE_DIR_NE;
				hc.layer_idx = 2;
			} else if (rat < 5.671f) {
				hc.layer_idx = 2;
				hc.edge_idx  = EDGE_DIR_N;
			} else {
				hc.edge_idx  = EDGE_DIR_N;
				hc.layer_idx = 1;
			}
		} else {
			if (rat > 5.671f) {
				hc.edge_idx  = EDGE_DIR_S;
				hc.layer_idx = 1;
			} else if (rat > 1.732f) {
				hc.edge_idx  = EDGE_DIR_S;
				hc.layer_idx = 0;
			} else if (rat > 0.839f) {
				hc.edge_idx  = EDGE_DIR_SW;
				hc.layer_idx = 0;
			} else if (rat > 0.364f) {
				hc.edge_idx  = EDGE_DIR_SW;
				hc.layer_idx = 1;
			} else if (rat > 0.0f) {
				hc.edge_idx  = EDGE_DIR_SW;
				hc.layer_idx = 2;
			} else if (rat > -0.364f) {
				hc.edge_idx  = EDGE_DIR_NW;
				hc.layer_idx = 0;
			} else if (rat > -0.839f) {
				hc.edge_idx  = EDGE_DIR_NW;
				hc.layer_idx = 1;
			} else if (rat > -1.732f) {
				hc.edge_idx  = EDGE_DIR_NW;
				hc.layer_idx = 2;
			} else if (rat > -5.671f) {
				hc.edge_idx  = EDGE_DIR_N;
				hc.layer_idx = 0;
			} else {
				hc.edge_idx  = EDGE_DIR_N;
				hc.layer_idx = 1;
			}
		}

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
			//struct gridcell *p_cell = gridstate_get_gridcell(p_st, &(p_hc->addr), 0);
			
#if 0
			if (p_cell != NULL) {
				int i;
				for (i = 0; i < EDGE_DIR_NUM; i++)
					gridcell_set_edge_flags(p_cell, i, EDGE_TYPE_NOTHING);
			}
#endif
		}

		if (g.IO.MouseClicked[1] && p_hc != NULL) {
			struct gridcell *p_cell = gridstate_get_gridcell(p_st, &(p_hc->addr), 1);
			if (p_cell != NULL) {
				/* x goes from -1 to 1 and y goes from -.866 to .866*/
				if (p_hc->pos_in_cell.x*p_hc->pos_in_cell.x + p_hc->pos_in_cell.y*p_hc->pos_in_cell.y < 0.433*0.433) {
					gridcell_are_layers_fused_toggle(p_cell);
					program_compile(p_prog, p_st);
				}
			}


#if 0
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
						switch (gridcell_get_edge_flags(p_nb, get_opposing_edge_id(p_hc->edge_idx))) {
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
						gridcell_set_edge_flags_adv(p_nb, p_cell, get_opposing_edge_id(p_hc->edge_idx), new_flag);
					}
				}

			}
#endif
		}




		if (make_active) {
			ImGui::SetActiveID(id, window);
			ImGui::SetFocusID(id, window);
			ImGui::FocusWindow(window);
		}
	}

	if (p_state->mouse_down) {
		ImVec2 drag = iv2_sub(mprel, p_state->mouse_down_pos);

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

    ImGui::RenderFrame(frame_bb.Min, frame_bb.Max, ImColor(255, 255, 255, 255), true, style.FrameRounding);

	ImDrawList *p_list = ImGui::GetWindowDrawList();

    p_list->PushClipRect(inner_bb.Min, inner_bb.Max, true);  // Render-level scissoring. This is passed down to your render function but not used for CPU-side coarse clipping. Prefer using higher-level ImGui::PushClipRect() to affect logic (hit-testing and widget culling)

	ImVec2 vMin = inner_bb.Min;
	ImVec2 vMax = inner_bb.Max;

	float window_width = vMax.x - vMin.x;
	float window_height = vMax.y - vMin.y;

	uint32_t cell_y = ((uint64_t)bl_y) >> 16;
	do {
		int64_t cy = ((int64_t)(int32_t)cell_y)*65536;
		float oy = (cy - bl_y)*radius*0.866f/65536.0f;

		if (oy - radius > window_height)
			break;

		uint32_t cell_x = ((uint64_t)bl_x) >> 16;
		do {
			int64_t cx = ((int64_t)(int32_t)cell_x)*65536;
			
			/* find the pixel coordinate of the centre of this element. */
			float ox = (cx - bl_x)*radius*3.0f/65536.0f;
			if (cell_y & 0x1)
				ox += radius*1.5f;

			/* if none of this object is visible, we're done. */
			if (ox - radius > window_width)
				break;

			ImVec2 p;

			p.x = vMin.x + ox;
			p.y = vMax.y - oy;


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

			float edge_length = radius;

			/* the edge length of the inner edge. constant. */
			float l = edge_length/1.366f;

			/* the distance between the midpoint of the inner edge and the next circle midpoint
			 * and is also the distance between the edge and the inner edge that all circles lie on. constant */
			float k = l/3.15470054f;

			/* the radius of circles on the inner edge if they were all touching (used for click regions) */
			float touching_radius = k/2.0f;

			/* the radius of the circles on the inner edge - this number could be between k/2 (touching) and probably k/3 */
			float r = k/2.8f;

			float a_grid_x[4];
			float a_grid_y[4];

			a_grid_x[0] = p.x - 0.5f*edge_length;
			a_grid_x[1] = p.x + 0.5f*edge_length;
			a_grid_x[2] = p.x + 1.0f*edge_length;
			a_grid_x[3] = p.x + 0.5f*edge_length;

			a_grid_y[0] = p.y - 0.866f*edge_length;
			a_grid_y[1] = p.y - 0.866f*edge_length;
			a_grid_y[2] = p.y;
			a_grid_y[3] = p.y + 0.866f*edge_length;

			{
				float a_x[3];
				float a_y[3];
				float b_x[3];
				float b_y[3];
				int i, j;
				int b_cursor_over_this_cell_stack;

				/* mouse pos relative to centre position */
				float mpxrc = io.MousePos.x - p.x;
				float mpyrc = io.MousePos.y - p.y;

				b_cursor_over_this_cell_stack =
					(   p_hc != NULL
					&&  p_hc->addr.x == cell_x
					&&  p_hc->addr.y == cell_y
					);

				/* points in this cell */
				a_x[0] = -k;
				a_y[0] = (1.0f-0.866f*3.15470054f*1.366f)*k;
				a_x[1] = 0.0;
				a_y[1] = (1.0f-0.866f*3.15470054f*1.366f)*k;
				a_x[2] = k;
				a_y[2] = (1.0f-0.866f*3.15470054f*1.366f)*k;

				/* points inside of the neighbouring cell */
				b_x[0] = -k;
				b_y[0] = (1.0f-0.866f*3.15470054f*1.366f-2)*k;
				b_x[1] = 0.0;
				b_y[1] = (1.0f-0.866f*3.15470054f*1.366f-2)*k;
				b_x[2] = k;
				b_y[2] = (1.0f-0.866f*3.15470054f*1.366f-2)*k;

				for (i = 0; i < 3; i++) { /* for each of the three edges */
					int             b_invalid_neighbour_addr;
					int             b_cursor_over_neighbour_stack;
					struct gridaddr neighbour_addr;
					struct gridaddr cell_addr;

					/* get information about the neighbour of this edge */
					cell_addr.x = cell_x;
					cell_addr.y = cell_y;
					cell_addr.z = 0;
					b_invalid_neighbour_addr = gridaddr_edge_neighbour(&neighbour_addr, &cell_addr, i);
					b_cursor_over_neighbour_stack =
						(   p_hc != NULL
						&&  p_hc->addr.x == neighbour_addr.x
						&&  p_hc->addr.y == neighbour_addr.y
						);

					/* logic for click operations for our cell */
					for (j = 0; j < 3; j++) { /* for each layer of this edge... */
						float xrc = mpxrc - (-k + j*k);
						float yrc = mpyrc - (k - 0.866f*edge_length);
						if  (   xrc > -touching_radius
						    &&  xrc < touching_radius
						    &&  (   (yrc > -k && yrc < 0.0f) /* in the square between the inner and outer hexagon edges */
						        ||  xrc*xrc + yrc*yrc < touching_radius*touching_radius /* in the circular landing shape at the end of the hexagon */
						        )
						    &&  g.IO.MouseClicked[0]
						    && !b_invalid_neighbour_addr
						    ) {
							struct gridcell *p_cell;
							cell_addr.z      = j;
							if ((p_cell = gridstate_get_gridcell(p_st, &cell_addr, 1)) != NULL) {
								if (!gridcell_set_neighbour_edge_connection_type(p_cell, i, get_next_connection_type(gridcell_get_neighbour_edge_connection_type(p_cell, i))))
									program_compile(p_prog, p_st);
							}
						}
					}

					/* logic for click operations for the neighbour cell */
					for (j = 0; j < 3; j++) { /* for each layer of this edge... */
						float xrc = mpxrc - (-k + j*k);
						float yrc = mpyrc - (-k - 0.866f*edge_length);
						if  (   xrc > -k*0.5f
						    &&  xrc < k*0.5f
						    &&  (   (yrc > 0.0 && yrc < k) /* in the square between the inner and outer hexagon edges */
						        ||  xrc*xrc + yrc*yrc < k*k*0.25f /* in the circular landing shape at the end of the hexagon */
						        )
						    &&  g.IO.MouseClicked[0]
						    && !b_invalid_neighbour_addr
						    ) {
							struct gridcell *p_neighbour;
							int edge_id = get_opposing_edge_id(i);
							neighbour_addr.z = j;
							if ((p_neighbour = gridstate_get_gridcell(p_st, &neighbour_addr, 1)) != NULL) {
								if (!gridcell_set_neighbour_edge_connection_type(p_neighbour, edge_id, get_next_connection_type(gridcell_get_neighbour_edge_connection_type(p_neighbour, edge_id))))
									program_compile(p_prog, p_st);
							}
						}
					}

					/* rendering */
					{
						struct gridcell *ap_cell[NUM_LAYERS];
						struct gridcell *ap_neighbours[NUM_LAYERS];
						int              a_busted_edges[NUM_LAYERS];
						int              a_grid_rgb[3];

						for (j = 0; j < NUM_LAYERS; j++) {
							cell_addr.z      = j;
							neighbour_addr.z = j;
							ap_cell[j]       = gridstate_get_gridcell(p_st, &cell_addr, 0);
							ap_neighbours[j] = gridstate_get_gridcell(p_st, &neighbour_addr, 0);
							a_busted_edges[j] = 0;
						}

						/* plot the actual grid line first. */
						a_grid_rgb[0] = 192 + ((a_busted_edges[1]||a_busted_edges[2]) ? -64 : 32);
						a_grid_rgb[1] = 192 + ((a_busted_edges[0]||a_busted_edges[2]) ? -64 : 32);
						a_grid_rgb[2] = 192 + ((a_busted_edges[0]||a_busted_edges[1]) ? -64 : 32);
						p_list->AddLine(ImVec2(a_grid_x[i], a_grid_y[i]), ImVec2(a_grid_x[i+1], a_grid_y[i+1]), ImColor(a_grid_rgb[0], a_grid_rgb[1], a_grid_rgb[2]), 2*edge_length/40);

						for (j = 0; j < NUM_LAYERS; j++) { /* for each layer of this edge... */
							struct gridcell *p_cell;
							struct gridcell *p_neighbour;
							cell_addr.z      = j;
							neighbour_addr.z = j;
							if  (   (p_cell = gridstate_get_gridcell(p_st, &cell_addr, 0)) != NULL
							    &&  (p_neighbour = gridstate_get_gridcell(p_st, &neighbour_addr, 0)) != NULL
							    ) {
								int ctype = gridcell_get_edge_connection_type(p_cell, i);
								int ntype = gridcell_get_edge_connection_type(p_neighbour, get_opposing_edge_id(i));
								int busted_edge = ((p_cell->data & GRIDCELL_PROGRAM_BROKEN_BIT_MASK(i)) || (p_neighbour->data & GRIDCELL_PROGRAM_BROKEN_BIT_MASK(i)));

								ImU32 layer_colour = ImColor
									(((j == 0) ? 196 : 128) - (busted_edge ? 64 : 0)
									,((j == 1) ? 196 : 128) - (busted_edge ? 64 : 0)
									,((j == 2) ? 196 : 128) - (busted_edge ? 64 : 0)
									,255
									);

								float centre_x = p.x + a_x[j];
								float centre_y = p.y + a_y[j];
								if (ctype == EDGE_LAYER_CONNECTION_NET_CONNECTED) {
									assert(ntype == EDGE_LAYER_CONNECTION_NET_CONNECTED);
									p_list->AddCircleFilled(ImVec2(centre_x, centre_y), r, layer_colour, 17);
									p_list->AddCircleFilled(ImVec2(p.x + b_x[j], p.y + b_y[j]), r, layer_colour, 17);
									p_list->AddLine(ImVec2(centre_x, centre_y), ImVec2(p.x + b_x[j], p.y + b_y[j]), layer_colour, r);
								} else if (ctype == EDGE_LAYER_CONNECTION_RECEIVES_DELAY_INVERTED) {
									assert(ntype == EDGE_LAYER_CONNECTION_SENDS);
									p_list->AddCircleFilled(ImVec2(p.x + b_x[j], p.y + b_y[j]), r, layer_colour, 17);
									p_list->AddLine(ImVec2(centre_x, centre_y), ImVec2(p.x + b_x[j], p.y + b_y[j]), layer_colour, r);
								} else if (ctype == EDGE_LAYER_CONNECTION_RECEIVES_INVERTED) {
									assert(ntype == EDGE_LAYER_CONNECTION_SENDS);
									p_list->AddCircleFilled(ImVec2(p.x + b_x[j], p.y + b_y[j]), r, layer_colour, 17);
									p_list->AddLine(ImVec2(centre_x, centre_y), ImVec2(p.x + b_x[j], p.y + b_y[j]), layer_colour, r);
								} else if (ntype == EDGE_LAYER_CONNECTION_RECEIVES_DELAY_INVERTED) {
									assert(ctype == EDGE_LAYER_CONNECTION_SENDS);
									p_list->AddCircleFilled(ImVec2(centre_x, centre_y), r, layer_colour, 17);
									p_list->AddLine(ImVec2(centre_x, centre_y), ImVec2(p.x + b_x[j], p.y + b_y[j]), layer_colour, r);
								} else if (ntype == EDGE_LAYER_CONNECTION_RECEIVES_INVERTED) {
									assert(ctype == EDGE_LAYER_CONNECTION_SENDS);
									p_list->AddCircleFilled(ImVec2(centre_x, centre_y), r, layer_colour, 17);
									p_list->AddLine(ImVec2(centre_x, centre_y), ImVec2(p.x + b_x[j], p.y + b_y[j]), layer_colour, r);
								} else {
									assert(ntype == EDGE_LAYER_CONNECTION_UNCONNECTED);
									assert(ctype == EDGE_LAYER_CONNECTION_UNCONNECTED);
								}



							} else if (b_cursor_over_this_cell_stack || b_cursor_over_neighbour_stack) {
								ImU32 layer_colour = ImColor
									(((j == 0) ? 196 : 128)
									,((j == 1) ? 196 : 128)
									,((j == 2) ? 196 : 128)
									,255
									);

								if (b_cursor_over_this_cell_stack) {
									float centre_x = p.x + a_x[j];
									float centre_y = p.y + a_y[j];
									p_list->AddCircleFilled(ImVec2(centre_x, centre_y),         r, layer_colour, 17);
								}
								if (b_cursor_over_neighbour_stack) {
									p_list->AddCircleFilled(ImVec2(p.x + b_x[j], p.y + b_y[j]), r, layer_colour, 17);
								}
							}

							vmpy(&(a_x[j]), &(a_y[j]), 0.5f, 0.866f);
							vmpy(&(b_x[j]), &(b_y[j]), 0.5f, 0.866f);
						}

					}

					/* rotate relative mouse position */
					vmpy(&mpxrc, &mpyrc, 0.5f, -0.866f);
				}



			}


#if 0
			/* pointing upwards */
			ImVec2 tmp1 = ImVec2(inner_n1.x + (inner_n2.x - inner_n1.x)/3, inner_n1.y);
			ImVec2 tmp2 = ImVec2(inner_n1.x + (inner_n2.x - inner_n1.x)/6, inner_n1.y - (inner_n2.x - inner_n1.x)/4);
			p_list->AddLine(inner_n1,  tmp2,  ImColor(0, 0, 0, 256), 2*edge_length/40);
			p_list->AddLine(tmp2,  tmp1,  ImColor(0, 0, 0, 256), 2*edge_length/40);

			/* pointing downwards */
			tmp1 = ImVec2(inner_n1.x + (inner_n2.x - inner_n1.x)/3, inner_n1.y);
			tmp2 = ImVec2(inner_n1.x + (inner_n2.x - inner_n1.x)/6, inner_n1.y + (inner_n2.x - inner_n1.x)/4);
			p_list->AddLine(inner_n1,  tmp2,  ImColor(0, 0, 0, 256), 2*edge_length/40);
			p_list->AddLine(tmp2,  tmp1,  ImColor(0, 0, 0, 256), 2*edge_length/40);
#endif



			struct gridaddr addr;
			addr.x = cell_x;
			addr.y = cell_y;
			addr.z = 0;
			struct gridcell *p_cell = gridstate_get_gridcell(p_st, &addr, 0);
			if (p_cell != NULL) {
				if (gridcell_are_layers_fused_get(p_cell)) {
					p_list->AddCircleFilled(p, radius*0.25, ImColor(128, 128, 128, 255), 17);

				}

			}

#if 0
			draw_edge_arrows(edge_mode_n, p.x, p.y, 0.0f, -0.866f, radius);
			draw_edge_arrows(edge_mode_ne, p.x, p.y, 0.75f, -0.433f, radius);
			draw_edge_arrows(edge_mode_se, p.x, p.y, 0.75f, +0.433f, radius);
#endif

#if 0
			if (cell_x == 0 && cell_y == 0)
				p_list->AddCircle(p, radius*0.5, ImColor(192, 0, 0, 255), 17, 2.0f*radius/40);
#endif

#if 0
			if (p_cell != NULL) {
				if (p_cell->flags & CELLFLAG_IS_COMPUTING_MASK)
					p_list->AddCircleFilled(p, radius*0.5, ImColor(255, 0, 128, 255), 17);
				else if (p_cell->flags & CELLFLAG_CURRENT_VALUE_MASK)
					p_list->AddCircleFilled(p, radius*0.1, ImColor(0, 255, 0, 255), 17);

			}
#endif



			cell_x += 1;
		} while (1);

		cell_y += 1;

	} while (1);

	p_list->PopClipRect();

	if (ImGui::IsItemHovered() && p_hc != NULL)
	{
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize()*35.0f);

		ImGui::Text
			("down=%d (lda=%f,%f) snap=(%ld,%ld) cell=(%f,%f) edge=%d errors=%d layer=%d"
			,p_state->mouse_down
			,mprel.x
			,mprel.y
			,(long)((int32_t)p_hc->addr.x) - 0x40000000
			,(long)((int32_t)p_hc->addr.y) - 0x40000000
			,p_hc->pos_in_cell.x
			,p_hc->pos_in_cell.y
			,p_hc->edge_idx
			,errors
			,p_hc->layer_idx
			);

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
		uint64_t id = gridaddr_to_id(x, y);
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


#if 0

static void draw_edge_arrows(int edge_mode_ne, float px, float py, float dvecx, float dvecy, float radius) {
	static const float r30x = 0.866f;
	static const float r30y = 0.5f;

	ImDrawList *p_list = ImGui::GetWindowDrawList();

	ImDrawListFlags old_flags = p_list->Flags;
	//p_list->Flags = ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLines;

#define SHAPE_SIZE (0.42f)
#define ARROW_SIZE (1.1f)

	if (edge_mode_ne) {
		float mx1 = px + dvecx*(radius*(1.0f-ARROW_SIZE*0.5f));
		float my1 = py + dvecy*(radius*(1.0f-ARROW_SIZE*0.5f));
		float mx2 = px + dvecx*(radius*(1.0f+ARROW_SIZE*0.5f));
		float my2 = py + dvecy*(radius*(1.0f+ARROW_SIZE*0.5f));

		float nvecx  = r30x*dvecx - r30y*dvecy;
		float nvecy  = r30x*dvecy + r30y*dvecx;
		float nvecx2 = r30x*dvecx + r30y*dvecy;
		float nvecy2 = r30x*dvecy - r30y*dvecx;

		float ax1, ay1, ax2, ay2;

		ImU32 c = ImColor(192, 192, 128, 255);

		if  (   edge_mode_ne == EDGE_RECEIVING_DF
			||  edge_mode_ne == EDGE_RECEIVING_F
			||  edge_mode_ne == EDGE_RECEIVING_DI
			||  edge_mode_ne == EDGE_RECEIVING_I
			) {
			ax1 = mx1 + nvecx*radius*SHAPE_SIZE;
			ay1 = my1 + nvecy*radius*SHAPE_SIZE;
			ax2 = mx1 + nvecx2*radius*SHAPE_SIZE;
			ay2 = my1 + nvecy2*radius*SHAPE_SIZE;

			if  (   edge_mode_ne == EDGE_RECEIVING_DF
				||  edge_mode_ne == EDGE_RECEIVING_F
				)
				p_list->AddTriangleFilled(ImVec2(mx1, my1), ImVec2(ax1, ay1), ImVec2(ax2, ay2), c);
			else
				p_list->AddTriangle(ImVec2(mx1, my1), ImVec2(ax1, ay1), ImVec2(ax2, ay2), c, 2.0f*radius/40.0f);

			mx1 = (ax1 + ax2)*0.5f;
			my1 = (ay1 + ay2)*0.5f;
		} else if (edge_mode_ne != EDGE_NOTHING) {
			ax1 = mx2 - nvecx*radius*SHAPE_SIZE;
			ay1 = my2 - nvecy*radius*SHAPE_SIZE;
			ax2 = mx2 - nvecx2*radius*SHAPE_SIZE;
			ay2 = my2 - nvecy2*radius*SHAPE_SIZE;

			if  (   edge_mode_ne == EDGE_SENDING_DF
				||  edge_mode_ne == EDGE_SENDING_F
				)
				p_list->AddTriangleFilled(ImVec2(mx2, my2), ImVec2(ax1, ay1), ImVec2(ax2, ay2), c);
			else
				p_list->AddTriangle(ImVec2(mx2, my2), ImVec2(ax1, ay1), ImVec2(ax2, ay2), c, 2.0f*radius/40.0f);

			mx2 = (ax1 + ax2)*0.5f;
			my2 = (ay1 + ay2)*0.5f;
		}

		if  (   edge_mode_ne == EDGE_SENDING_DF
			||  edge_mode_ne == EDGE_SENDING_DI
			) {
			float r = radius*SHAPE_SIZE*0.5f;
			p_list->AddCircle(ImVec2(mx1 + dvecx*r, my1 + dvecy*r), r, c, 11, 2.0f*radius/40.0f);
			mx1 += dvecx*r*2.0f;
			my1 += dvecy*r*2.0f;
		
		} else if
			(   edge_mode_ne == EDGE_RECEIVING_DF
			||  edge_mode_ne == EDGE_RECEIVING_DI
			) {
			float r = radius*SHAPE_SIZE*0.5f;
			p_list->AddCircle(ImVec2(mx2 - dvecx*r, my2 - dvecy*r), r, c, 11, 2.0f*radius/40.0f);
			mx2 -= dvecx*r*2.0f;
			my2 -= dvecy*r*2.0f;
		}

		assert(isnormal(mx1));
		assert(isnormal(my1));
		assert(isnormal(mx2));
		assert(isnormal(my2));

		p_list->AddLine(ImVec2(mx1, my1), ImVec2(mx2, my2), c, 2.0f*radius/40.0f);
	}


	p_list->Flags = old_flags;
}

#endif

