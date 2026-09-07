"""Small geometric predicates shared by placement and circulation checks."""
import math


def rectangle(x, y, width, depth, heading=0):
    c, s = math.cos(math.radians(heading)), math.sin(math.radians(heading))
    return [(x + u*c - v*s, y + u*s + v*c)
            for u, v in [(-width/2, -depth/2), (width/2, -depth/2),
                         (width/2, depth/2), (-width/2, depth/2)]]


def polygons_overlap(a, b):
    for polygon in (a, b):
        for p, q in zip(polygon, polygon[1:] + polygon[:1]):
            nx, ny = -(q[1]-p[1]), q[0]-p[0]
            pa = [x*nx + y*ny for x, y in a]
            pb = [x*nx + y*ny for x, y in b]
            if max(pa) <= min(pb) + 1e-8 or max(pb) <= min(pa) + 1e-8:
                return False
    return True


def footprint(item, bounds):
    heading = float(item["rotation"][2])
    c, s = math.cos(math.radians(heading)), math.sin(math.radians(heading))
    u, v = (bounds[0][0]+bounds[1][0])/2, (bounds[0][1]+bounds[1][1])/2
    x, y = item["position"][:2]
    return rectangle(x+u*c-v*s, y+u*s+v*c,
                     bounds[1][0]-bounds[0][0], bounds[1][1]-bounds[0][1], heading)


def circle_intersects_polygon(x, y, radius, polygon):
    # Convex polygon containment, then distance to its edges.
    crosses = [(q[0]-p[0])*(y-p[1]) - (q[1]-p[1])*(x-p[0])
               for p, q in zip(polygon, polygon[1:]+polygon[:1])]
    if all(v >= 0 for v in crosses) or all(v <= 0 for v in crosses):
        return True
    for p, q in zip(polygon, polygon[1:]+polygon[:1]):
        dx, dy = q[0]-p[0], q[1]-p[1]
        t = max(0, min(1, ((x-p[0])*dx+(y-p[1])*dy)/(dx*dx+dy*dy or 1)))
        if math.hypot(x-p[0]-t*dx, y-p[1]-t*dy) < radius:
            return True
    return False


def relative_offset(parent_bounds, child_bounds, parent_heading, child_heading,
                    relation, gap=0, front_heading=0):
    """Model-origin offset in parent coordinates; uses rotated footprint extents.

    Functional front is annotated degrees CCW from local +Y. Tilt is handled by
    callers rejecting unsupported pitch/roll, never silently flattening bounds.
    """
    if not math.isfinite(gap) or gap < 0:
        raise ValueError('relative gap must be finite and non-negative')
    def extents(bounds, angle):
        c, s = math.cos(math.radians(angle)), math.sin(math.radians(angle))
        points = [(u*c-v*s,u*s+v*c) for u in (bounds[0][0],bounds[1][0])
                  for v in (bounds[0][1],bounds[1][1])]
        return [min(p[0] for p in points),min(p[1] for p in points)], [max(p[0] for p in points),max(p[1] for p in points)]
    p = extents(parent_bounds, -front_heading)
    q = extents(child_bounds, child_heading-parent_heading-front_heading)
    x = (p[0][0]+p[1][0]-q[0][0]-q[1][0])/2
    y = (p[0][1]+p[1][1]-q[0][1]-q[1][1])/2
    z = 0
    if relation == 'on_top':
        z = parent_bounds[1][2]-child_bounds[0][2]+gap
    elif relation == 'in_front': y = p[1][1]-q[0][1]+gap
    elif relation == 'behind': y = p[0][1]-q[1][1]-gap
    elif relation == 'right': x = p[1][0]-q[0][0]+gap
    elif relation == 'left': x = p[0][0]-q[1][0]-gap
    else: raise ValueError('unknown relative relation')
    c,s=math.cos(math.radians(front_heading)),math.sin(math.radians(front_heading))
    return x*c-y*s,x*s+y*c,z


def facing_heading(x, y, target, front_heading=0):
    if len(target) != 2 or not all(math.isfinite(float(v)) for v in [x,y,*target,front_heading]):
        raise ValueError('face_towards requires finite target xy and front heading')
    dx,dy=target[0]-x,target[1]-y
    if math.hypot(dx,dy) < 1e-8:
        raise ValueError('cannot face a coincident target')
    return (math.degrees(math.atan2(-dx,dy))-front_heading) % 360
