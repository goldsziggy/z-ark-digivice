"""Parametric protective direct-PWR recess; no captive moving actuator.

The fingertip solid is an illustrative screening gauge, not hand anthropometry.
Coordinates use the official board's PWR face and normal. Millimetres.
"""
import math
import numpy as np
import manifold3d as m

def build_access(P, outline, split_z, front_z):
    q=P['pwr_access'];a=math.radians(q['axis_angle_deg'])
    U=np.array([math.cos(a),math.sin(a),0.]);V=np.array([-U[1],U[0],0.])
    origin=q['face_radius_u']*U+np.array([0.,0.,q['face_z']])
    transform=np.column_stack((np.column_stack((V,[0,0,1],U)),origin))
    L=q['nose_length'];w=q['nose_width']/2;h=q['nose_height']/2
    nose=m.Manifold.sphere(1,96).scale((w,h,L)).translate((0,0,L))
    body=m.Manifold.cylinder(q['body_length_from_face']-L,1,q['body_flare'],96).scale((w,h,1)).transform([[1,0,0,0],[0,1,q['body_rise'],0],[0,0,1,0]]).translate((0,0,L))
    gauge=(nose+body).transform(transform)
    opening=gauge.minkowski_sum(m.Manifold.sphere(q['finger_allowance'],24))
    outer=gauge.minkowski_sum(m.Manifold.sphere(q['finger_allowance']+q['guard_wall'],24))
    start=q['guard_start_u']-q['face_radius_u']
    axial_clip=m.Manifold.cube((100,100,80-start),True).translate((0,0,(80+start)/2)).transform(transform)
    shell_clip=outline.extrude(front_z-split_z).translate((0,0,split_z))
    guard=(outer-opening)^axial_clip^shell_clip
    guard_clearance=guard.minkowski_sum(m.Manifold.sphere(.25,16))
    return dict(gauge=gauge,opening=opening,guard=guard,guard_clearance=guard_clearance,transform=transform.tolist())
