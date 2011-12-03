#include <float.h>

extern "C"
{
#include "tr_local.h"
}

/*
=================
R_MirrorPoint
=================
*/
static void R_MirrorPoint (vec3_t in, orientation_t *surface, orientation_t *camera, vec3_t out)
{
    int		i;
    vec3_t	local;
    vec3_t	transformed;
    float	d;

    VectorSubtract( in, surface->origin, local );

    VectorClear( transformed );
    for ( i = 0 ; i < 3 ; i++ ) {
        d = DotProduct(local, surface->axis[i]);
        VectorMA( transformed, d, camera->axis[i], transformed );
    }

    VectorAdd( transformed, camera->origin, out );
}

static void R_MirrorVector (vec3_t in, orientation_t *surface, orientation_t *camera, vec3_t out)
{
    int		i;
    float	d;

    VectorClear( out );
    for ( i = 0 ; i < 3 ; i++ ) {
        d = DotProduct(in, surface->axis[i]);
        VectorMA( out, d, camera->axis[i], out );
    }
}


/*
=============
R_PlaneForSurface
=============
*/
static void R_PlaneForSurface (surfaceType_t *surfType, cplane_t *plane)
{
    srfTriangles_t	*tri;
    srfPoly_t		*poly;
    drawVert_t		*v1, *v2, *v3;
    vec4_t			plane4;

    if (!surfType) {
        Com_Memset (plane, 0, sizeof(*plane));
        plane->normal[0] = 1;
        return;
    }
    switch (*surfType) {
    case SF_FACE:
        *plane = ((srfSurfaceFace_t *)surfType)->plane;
        return;
    case SF_TRIANGLES:
        tri = (srfTriangles_t *)surfType;
        v1 = tri->verts + tri->indexes[0];
        v2 = tri->verts + tri->indexes[1];
        v3 = tri->verts + tri->indexes[2];
        PlaneFromPoints( plane4, v1->xyz, v2->xyz, v3->xyz );
        VectorCopy( plane4, plane->normal ); 
        plane->dist = plane4[3];
        return;
    case SF_POLY:
        poly = (srfPoly_t *)surfType;
        PlaneFromPoints( plane4, poly->verts[0].xyz, poly->verts[1].xyz, poly->verts[2].xyz );
        VectorCopy( plane4, plane->normal ); 
        plane->dist = plane4[3];
        return;
    default:
        Com_Memset (plane, 0, sizeof(*plane));
        plane->normal[0] = 1;		
        return;
    }
}

/*
=================
R_GetPortalOrientation

entityNum is the entity that the portal surface is a part of, which may
be moving and rotating.

Returns qtrue if it should be mirrored
=================
*/
static qboolean R_GetPortalOrientations(drawSurf_t *drawSurf, int entityNum, 
                                        orientation_t *surface, orientation_t *camera,
                                        vec3_t pvsOrigin, qboolean *mirror )
{
    int			i;
    cplane_t	originalPlane, plane;
    trRefEntity_t	*e;
    float		d;
    vec3_t		transformed;

    // create plane axis for the portal we are seeing
    R_PlaneForSurface( drawSurf->surface, &originalPlane );

    // rotate the plane if necessary
    if ( entityNum != ENTITYNUM_WORLD ) {
        tr.currentEntityNum = entityNum;
        tr.currentEntity = &tr.refdef.entities[entityNum];

        // get the orientation of the entity
        R_RotateForEntity( tr.currentEntity, &tr.viewParms, &tr.or );

        // rotate the plane, but keep the non-rotated version for matching
        // against the portalSurface entities
        R_LocalNormalToWorld( originalPlane.normal, plane.normal );
        plane.dist = originalPlane.dist + DotProduct( plane.normal, tr.or.origin );

        // translate the original plane
        originalPlane.dist = originalPlane.dist + DotProduct( originalPlane.normal, tr.or.origin );
    } else {
        plane = originalPlane;
    }

    VectorCopy( plane.normal, surface->axis[0] );
    PerpendicularVector( surface->axis[1], surface->axis[0] );
    CrossProduct( surface->axis[0], surface->axis[1], surface->axis[2] );

    // locate the portal entity closest to this plane.
    // origin will be the origin of the portal, origin2 will be
    // the origin of the camera
    for ( i = 0 ; i < tr.refdef.num_entities ; i++ ) {
        e = &tr.refdef.entities[i];
        if ( e->e.reType != RT_PORTALSURFACE ) {
            continue;
        }

        d = DotProduct( e->e.origin, originalPlane.normal ) - originalPlane.dist;
        if ( d > 64 || d < -64) {
            continue;
        }

        // get the pvsOrigin from the entity
        VectorCopy( e->e.oldorigin, pvsOrigin );

        // if the entity is just a mirror, don't use as a camera point
        if ( e->e.oldorigin[0] == e->e.origin[0] && 
            e->e.oldorigin[1] == e->e.origin[1] && 
            e->e.oldorigin[2] == e->e.origin[2] ) {
                VectorScale( plane.normal, plane.dist, surface->origin );
                VectorCopy( surface->origin, camera->origin );
                VectorSubtract( vec3_origin, surface->axis[0], camera->axis[0] );
                VectorCopy( surface->axis[1], camera->axis[1] );
                VectorCopy( surface->axis[2], camera->axis[2] );

                *mirror = qtrue;
                return qtrue;
        }

        // project the origin onto the surface plane to get
        // an origin point we can rotate around
        d = DotProduct( e->e.origin, plane.normal ) - plane.dist;
        VectorMA( e->e.origin, -d, surface->axis[0], surface->origin );

        // now get the camera origin and orientation
        VectorCopy( e->e.oldorigin, camera->origin );
        AxisCopy( e->e.axis, camera->axis );
        VectorSubtract( vec3_origin, camera->axis[0], camera->axis[0] );
        VectorSubtract( vec3_origin, camera->axis[1], camera->axis[1] );

        // optionally rotate
        if ( e->e.oldframe ) {
            // if a speed is specified
            if ( e->e.frame ) {
                // continuous rotate
                d = (tr.refdef.time/1000.0f) * e->e.frame;
                VectorCopy( camera->axis[1], transformed );
                RotatePointAroundVector( camera->axis[1], camera->axis[0], transformed, d );
                CrossProduct( camera->axis[0], camera->axis[1], camera->axis[2] );
            } else {
                // bobbing rotate, with skinNum being the rotation offset
                d = sin( tr.refdef.time * 0.003f );
                d = e->e.skinNum + d * 4;
                VectorCopy( camera->axis[1], transformed );
                RotatePointAroundVector( camera->axis[1], camera->axis[0], transformed, d );
                CrossProduct( camera->axis[0], camera->axis[1], camera->axis[2] );
            }
        }
        else if ( e->e.skinNum ) {
            d = e->e.skinNum;
            VectorCopy( camera->axis[1], transformed );
            RotatePointAroundVector( camera->axis[1], camera->axis[0], transformed, d );
            CrossProduct( camera->axis[0], camera->axis[1], camera->axis[2] );
        }
        *mirror = qfalse;
        return qtrue;
    }

    // if we didn't locate a portal entity, don't render anything.
    // We don't want to just treat it as a mirror, because without a
    // portal entity the server won't have communicated a proper entity set
    // in the snapshot

    // unfortunately, with local movement prediction it is easily possible
    // to see a surface before the server has communicated the matching
    // portal surface entity, so we don't want to print anything here...

    //ri.Printf( PRINT_ALL, "Portal surface without a portal entity\n" );

    return qfalse;
}

static qboolean IsMirror( const drawSurf_t *drawSurf, int entityNum )
{
    int			i;
    cplane_t	originalPlane, plane;
    trRefEntity_t	*e;
    float		d;

    // create plane axis for the portal we are seeing
    R_PlaneForSurface( drawSurf->surface, &originalPlane );

    // rotate the plane if necessary
    if ( entityNum != ENTITYNUM_WORLD ) 
    {
        tr.currentEntityNum = entityNum;
        tr.currentEntity = &tr.refdef.entities[entityNum];

        // get the orientation of the entity
        R_RotateForEntity( tr.currentEntity, &tr.viewParms, &tr.or );

        // rotate the plane, but keep the non-rotated version for matching
        // against the portalSurface entities
        R_LocalNormalToWorld( originalPlane.normal, plane.normal );
        plane.dist = originalPlane.dist + DotProduct( plane.normal, tr.or.origin );

        // translate the original plane
        originalPlane.dist = originalPlane.dist + DotProduct( originalPlane.normal, tr.or.origin );
    } 
    else 
    {
        plane = originalPlane;
    }

    // locate the portal entity closest to this plane.
    // origin will be the origin of the portal, origin2 will be
    // the origin of the camera
    for ( i = 0 ; i < tr.refdef.num_entities ; i++ ) 
    {
        e = &tr.refdef.entities[i];
        if ( e->e.reType != RT_PORTALSURFACE ) {
            continue;
        }

        d = DotProduct( e->e.origin, originalPlane.normal ) - originalPlane.dist;
        if ( d > 64 || d < -64) {
            continue;
        }

        // if the entity is just a mirror, don't use as a camera point
        if ( e->e.oldorigin[0] == e->e.origin[0] && 
            e->e.oldorigin[1] == e->e.origin[1] && 
            e->e.oldorigin[2] == e->e.origin[2] ) 
        {
            return qtrue;
        }

        return qfalse;
    }
    return qfalse;
}

/*
** SurfIsOffscreen
**
** Determines if a surface is completely offscreen.
*/
static qboolean SurfIsOffscreen( const drawSurf_t *drawSurf, vec4_t clipDest[128] )
{
    float shortest = 100000000;
    int entityNum;
    int numTriangles;
    shader_t *shader;
    int		fogNum;
    int dlighted;
    vec4_t clip, eye;
    int i;
    unsigned int pointOr = 0;
    unsigned int pointAnd = (unsigned int)~0;

    if ( glConfig.smpActive ) {		// FIXME!  we can't do RB_BeginSurface/RB_EndSurface stuff with smp!
        return qfalse;
    }

    R_RotateForViewer();

    R_DecomposeSort( drawSurf->sort, &entityNum, &shader, &fogNum, &dlighted );
    RB_BeginSurface( shader, fogNum );
    rb_surfaceTable[ *drawSurf->surface ]( drawSurf->surface );

    assert( tess.numVertexes < 128 );

    for ( i = 0; i < tess.numVertexes; i++ )
    {
        int j;
        unsigned int pointFlags = 0;

        R_TransformModelToClip( tess.xyz[i], tr.or.modelMatrix, tr.viewParms.projectionMatrix, eye, clip );

        for ( j = 0; j < 3; j++ )
        {
            if ( clip[j] >= clip[3] )
            {
                pointFlags |= (1 << (j*2));
            }
            else if ( clip[j] <= -clip[3] )
            {
                pointFlags |= ( 1 << (j*2+1));
            }
        }
        pointAnd &= pointFlags;
        pointOr |= pointFlags;
    }

    // trivially reject
    if ( pointAnd )
    {
        return qtrue;
    }

    // determine if this surface is backfaced and also determine the distance
    // to the nearest vertex so we can cull based on portal range.  Culling
    // based on vertex distance isn't 100% correct (we should be checking for
    // range to the surface), but it's good enough for the types of portals
    // we have in the game right now.
    numTriangles = tess.numIndexes / 3;

    for ( i = 0; i < tess.numIndexes; i += 3 )
    {
        vec3_t normal;
        float dot;
        float len;

        VectorSubtract( tess.xyz[tess.indexes[i]], tr.viewParms.or.origin, normal );

        len = VectorLengthSquared( normal );			// lose the sqrt
        if ( len < shortest )
        {
            shortest = len;
        }

        if ( ( dot = DotProduct( normal, tess.normal[tess.indexes[i]] ) ) >= 0 )
        {
            numTriangles--;
        }
    }
    if ( !numTriangles )
    {
        return qtrue;
    }

    // mirrors can early out at this point, since we don't do a fade over distance
    // with them (although we could)
    if ( IsMirror( drawSurf, entityNum ) )
    {
        return qfalse;
    }

    if ( shortest > (tess.shader->portalRange*tess.shader->portalRange) )
    {
        return qtrue;
    }

    return qfalse;
}

/*
========================
R_MirrorViewBySurface

Returns qtrue if another view has been rendered
========================
*/
qboolean R_MirrorViewBySurface (drawSurf_t *drawSurf, int entityNum)
{
    vec4_t			clipDest[128];
    viewParms_t		newParms;
    viewParms_t		oldParms;
    orientation_t	surface, camera;

    // don't recursively mirror
    if (tr.viewParms.isPortal) {
        ri.Printf( PRINT_DEVELOPER, "WARNING: recursive mirror/portal found\n" );
        return qfalse;
    }

    if ( r_noportals->integer || (r_fastsky->integer == 1) ) {
        return qfalse;
    }

    // trivially reject portal/mirror
    if ( SurfIsOffscreen( drawSurf, clipDest ) ) {
        return qfalse;
    }

    // save old viewParms so we can return to it after the mirror view
    oldParms = tr.viewParms;

    newParms = tr.viewParms;
    newParms.isPortal = qtrue;
    if ( !R_GetPortalOrientations( drawSurf, entityNum, &surface, &camera, 
        newParms.pvsOrigin, &newParms.isMirror ) ) {
            return qfalse;		// bad portal, no portalentity
    }

    R_MirrorPoint (oldParms.or.origin, &surface, &camera, newParms.or.origin );

    VectorSubtract( vec3_origin, camera.axis[0], newParms.portalPlane.normal );
    newParms.portalPlane.dist = DotProduct( camera.origin, newParms.portalPlane.normal );

    R_MirrorVector (oldParms.or.axis[0], &surface, &camera, newParms.or.axis[0]);
    R_MirrorVector (oldParms.or.axis[1], &surface, &camera, newParms.or.axis[1]);
    R_MirrorVector (oldParms.or.axis[2], &surface, &camera, newParms.or.axis[2]);

    // OPTIMIZE: restrict the viewport on the mirrored view

    // render the mirror view
    R_RenderView (&newParms);

    tr.viewParms = oldParms;

    return qtrue;
}
