/* Exercise the exact Wine server helper with cross-process layered windows.
 * Wine LGPL-2.1-or-later. Geometry is deliberately independent of a GUI session. */
#include <stdio.h>
struct rectangle { int left, top, right, bottom; };
struct region { struct rectangle rect; };
struct process { int id; };
struct thread { struct process *process; };
struct window {
    struct rectangle window_rect, client_rect, surface_rect;
    unsigned int paint_flags;
    struct thread *thread;
};
enum { PAINT_HAS_LAYERED_SURFACE = 1 };
static int intersect_rect(struct rectangle *out, const struct rectangle *a,
                          const struct rectangle *b)
{
    struct rectangle r = {
        a->left > b->left ? a->left : b->left,
        a->top > b->top ? a->top : b->top,
        a->right < b->right ? a->right : b->right,
        a->bottom < b->bottom ? a->bottom : b->bottom
    };
    if (r.left >= r.right || r.top >= r.bottom) r = (struct rectangle){0};
    *out = r;
    return r.right > r.left && r.bottom > r.top;
}
static void set_region_rect(struct region *region, const struct rectangle *rect)
{
    region->rect = *rect;
}
#include "extracted_layered.c"
int main(void)
{
    struct process parent = {1}, child = {2};
    struct thread owner = {&parent};
    struct window win = {{0, 0, 400, 300}, {10, 20, 390, 280}, {0, 0, 1, 1},
                         PAINT_HAS_LAYERED_SURFACE, &owner};
    struct region region;
    unsigned int failed = 0;
    set_region_client_rect(&region, &win, &child);
    /* A foreign child gets the real client area, even with a dummy parent surface. */
    failed += region.rect.left != 10 || region.rect.top != 20 ||
              region.rect.right != 390 || region.rect.bottom != 280;
    set_region_client_rect(&region, &win, &parent);
    /* Same-process painting retains surface clipping. */
    failed += region.rect.right != 0 || region.rect.bottom != 0;
    win.paint_flags = 0;
    set_region_client_rect(&region, &win, &child);
    /* Ordinary windows also retain surface clipping. */
    failed += region.rect.right != 0 || region.rect.bottom != 0;
    win.paint_flags = PAINT_HAS_LAYERED_SURFACE;
    win.thread = NULL;
    set_region_client_rect(&region, &win, &child);
    /* Ownerless windows keep their previous behavior. */
    failed += region.rect.right != 0 || region.rect.bottom != 0;
    win.thread = &owner;
    win.surface_rect = (struct rectangle){0, 0, 400, 300};
    win.window_rect.right = 200;
    set_region_client_rect(&region, &win, &child);
    /* Skipping surface clipping must still obey the actual window rectangle. */
    failed += region.rect.right != 200 || region.rect.bottom != 280;
    printf("Layered clipping checks: 5, failures: %u\n", failed);
    return failed != 0;
}
