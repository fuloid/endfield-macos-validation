static void set_region_client_rect( struct region *region, struct window *win, struct process *process )
{
    struct rectangle rect;

    intersect_rect( &rect, &win->window_rect, &win->client_rect );
    if (!(win->paint_flags & PAINT_HAS_LAYERED_SURFACE) || !win->thread || win->thread->process == process)
        intersect_rect( &rect, &rect, &win->surface_rect );
    set_region_rect( region, &rect );
}
