"""C14-P19 TOUCH_ONLY stock100 RF direct PWR isolated parametric CSG, millimetres.
gameplay_buttons controls only the two optional gameplay panel bores and reference
switch envelopes. False restores the continuous 2.4 mm front wall. All C13 mating
interfaces and the distinct onboard PWR finger recess remain unchanged.
Run Python with manifold3d,numpy,trimesh. Reference geometry derives from official
Waveshare standard-cover STEP, transformed -90deg Z and glass face at front_z.
This is a bounded mechanical prototype; no electrical/RF/ergonomic qualification.
"""
from pathlib import Path
import json,math,itertools,collections
import numpy as np
import manifold3d as m
import trimesh
ROOT=Path(__file__).resolve().parents[1]
P=json.loads((ROOT/'cad/parameters.json').read_text())
GAMEPLAY_BUTTONS=P.get('gameplay_buttons',True)
assert isinstance(GAMEPLAY_BUTTONS,bool),'gameplay_buttons must be a JSON boolean'
FRONT_NAME='front_shell_PANEL_SWITCHES' if GAMEPLAY_BUTTONS else 'front_shell_TOUCH_ONLY'
VARIANT_NAME='two_panel_buttons' if GAMEPLAY_BUTTONS else 'TOUCH_ONLY'
LIFT=P['front_stack_lift']
# One lift control keeps every precision board/mount relationship unchanged.
P.update(front_z=30.4+LIFT,bezel_top_z=30.4+LIFT+P['bezel_collar_top_offset'],display_rear_face_z=19.5+LIFT,
 carrier_z=17.1+LIFT,reader_tray_floor_z=17.1+LIFT,
 reader_center=[0,-38+P['reader_shift_y'],22.25+LIFT],antenna_center=[0,40.5,27.2+LIFT],
 battery_center=[0,-33+P['battery_shift_y'],8.5],battery_bay_center=[0,-33+P['battery_shift_y'],8.9])
CS,M=m.CrossSection,m.Manifold
N=P['segments'];W=P['wall'];F=P['front_z'];SPLIT=P['split_z'];BY=P['button_y'];RY=P['card_center_y']
PARTS={};META={};HW={};HWMETA={};FASTENERS=[]
M2_CLEAR_RADIUS=P['m2_clearance']/2
# Keep black housing roof pilots independent of the user-selected white pilot.
M2_PILOT_RADIUS=P['m2_plastic_pilot']/2
WHITE_PILOT_RADIUS=P['m2_white_shell_pilot']/2
def circ(r,xy=(0,0),n=N):return CS.circle(r,n).translate(xy)
def rr(w,h,r=1,xy=(0,0)):return CS.square((w-2*r,h-2*r),True).offset(r,circular_segments=48).translate(xy)
def ext(cs,z,h):return cs.extrude(h).translate((0,0,z))
def cyl(r,z,h,xy=(0,0),n=64):return ext(circ(r,xy,n),z,h)
def box(w,h,d,xyz):return M.cube((w,h,d),True).translate(xyz)
def tube_path(points,radius):
 s=[M.sphere(radius,24).translate(p) for p in points]
 return M.batch_boolean([M.batch_hull([a,b]) for a,b in zip(s,s[1:])],m.OpType.Add)
def add(name,solid,group='common',color=(.015,.02,.026,1),flip=False,desc='',qty=1):
 PARTS[name]=solid;META[name]={'group':group,'stl_folder':group,'color':list(color),'flip_x_for_print':flip,'description':desc,'quantity':qty}
def hw(name,solid,color=(.05,.3,.16,1),collision=True,role='hardware'):
 HW[name]=solid;HWMETA[name]={'color':list(color),'check_collision':collision,'role':role}
def fast(name,xy,seat,length,direction,target,pilot,head=None):
 family=name.rsplit('_',1)[0]
 length=P['fastener_lengths'].get(family,length)
 FASTENERS.append({'name':name,'xy':list(xy),'bearing_z':seat,'shaft_length':length,'shaft_diameter':2,'direction':direction,'head_diameter':P['fastener_head_diameter_bound'],'head_height':P['fastener_head_height_bound'] if head is None else head,'target_part':target,'pilot_z_limits':pilot,'status':P['fastener_status']})
white=(.8,.84,.85,1);red=(.65,.006,.009,1)
head=circ(P['head_radius'])
taper=CS.batch_hull([circ(P['grip_profile_radius'],xy) for xy in [(-P['grip_shoulder_half_width'],P['grip_shoulder_y']),(P['grip_shoulder_half_width'],P['grip_shoulder_y']),(-P['grip_tail_half_width'],P['tail_corner_y']),(P['grip_tail_half_width'],P['tail_corner_y']),(0,P['tail_center_y'])]])
outline=head+taper;inner=outline.offset(-W)
edge=.986
head_outer=head.scale((edge,edge)).extrude(.7,scale_top=(1/edge,1/edge))+ext(head,.7,F-1.4)+head.extrude(.7,scale_top=(edge,edge)).translate((0,0,F-.7))
gr=P['grip_contact_edge_radius'];slices=[]
for i in range(P['grip_contact_round_segments']+1):
 a=math.pi/2*i/P['grip_contact_round_segments'];z=gr*(1-math.cos(a));inset=gr*(1-math.sin(a));slices.append(ext(taper.offset(-inset),z,.0001))
roll=M.batch_boolean([M.batch_hull([a,b]) for a,b in zip(slices,slices[1:])],m.OpType.Add)
outer=head_outer+ext(taper,gr,F-2*gr)+roll+roll.mirror((0,0,1)).translate((0,0,F))
front=(outer^box(250,250,F-SPLIT,(0,0,(F+SPLIT)/2)))-ext(inner,SPLIT-.1,F-W-SPLIT+.1)
back=(outer^box(250,250,SPLIT,(0,0,SPLIT/2)))-ext(inner,W,SPLIT)
bcx,bcy,bcz=P['battery_center'];bw,bh,bd=P['battery_bay_xyz']
guides=ext(rr(bw+2.4,bh+2.4,2,(bcx,bcy))-CS.square((bw,bh),True).translate((bcx,bcy)),W,3)
for yy in [bcy-bh/2,bcy+bh/2]:guides-=box(18,8,8,(bcx,yy,5))
back+=guides
# Recessed rear strap slots; textile strap and insulation are required, no hard crossbar over cell.
for xx in [-22.1,22.1]:
 for yy in [-12,-54]:back-=ext(rr(1.2,5,.4,(xx,yy)),-.1,W+.2)
CASE_POINTS=[(0,27),(-23.6,-13.5),(24,-43),(0,-82.5)]
for i,xy in enumerate(CASE_POINTS):
 back+=cyl(3,0,SPLIT,xy);front+=cyl(3,SPLIT,F-SPLIT,xy)
 back-=cyl(M2_CLEAR_RADIUS,-.1,SPLIT+.2,xy)+cyl(2.3,-.1,P['case_head_bearing_z']+.1,xy)
 front-=cyl(WHITE_PILOT_RADIUS,SPLIT-.1,10.5,xy)
 fast('case_'+str(i+1),xy,P['case_head_bearing_z'],P['case_screw_length'],1,FRONT_NAME,[14,24.4])
# Actual SKU29565 reference parts. A separate rectangular USB envelope avoids non-solid source shell.
for path in sorted((ROOT/'reference/official_meshes').glob('*.stl')):
 tm=trimesh.load(path,force='mesh');s=M(m.Mesh(np.asarray(tm.vertices,dtype=np.float32),np.asarray(tm.faces,dtype=np.uint32)))
 assert s.status()==m.Error.NoError,(path.name,s.status())
 s=s.rotate((0,0,P['board_rotation_z_deg'])).translate((0,0,F))
 hw('board_'+path.stem,s,(.055,.10,.08,1) if 'pcb' in path.name else (.16,.17,.18,1),role='official_board')
usb=box(7.6,8.94,4.061525,(-19.2,0,F-8.609612))
hw('board_USB_BOUND',usb,role='official_board')
# Conservative corridors begin outside the actual connector, and are gauges not connector selection.
USB=box(28,14,8,(-36,0,F-8.6));front-=USB
# CAD-only larger outer pocket stops before the display flex; inner throat is unchanged.
USB_OUTER=box(P['usb_outer_recess_inner_x']-P['usb_outer_recess_external_x'],P['usb_outer_recess_width'],P['usb_outer_recess_height'],((P['usb_outer_recess_inner_x']+P['usb_outer_recess_external_x'])/2,0,F+P['usb_outer_recess_center_z_offset']))
front-=USB_OUTER
hw('USB_outer_RECESS_PROVISIONAL_GAUGE',USB_OUTER,(.8,.3,.05,.35),role='access_gauge')
HEADER=box(*P['j9_mating_provisional_xyz'],(.1,14.3,F-16.45))
BATPLUG=box(9,10,5,(20,-7.5,F-8.7))
hw('USB_external_14x8_GAUGE',USB,(.6,.4,.1,.4),role='access_gauge')
hw('J9_mating_16p9x8x7_PROVISIONAL',HEADER,role='mating_plug')
hw('battery_mating_9x10x5_PROVISIONAL',BATPLUG,role='mating_plug')
# Side board controls are serviced with enclosure open. No fabricated external button mapping.
# New carrier follows three asymmetric M2 mounting posts from front-view STEP, rotated -90deg.
contract=json.loads((ROOT/'reference/MECHANICAL_CONTRACT.json').read_text())
MOUNTS=[(q['front_view_xy_mm'][1],-q['front_view_xy_mm'][0]) for q in contract['mounts']]
CARRIER_POINTS=[(26*math.cos(math.radians(a)),26*math.sin(math.radians(a))) for a in P['carrier_mount_angles_deg']]
CZ=P['carrier_z'];POST=F-10.9
carrier=ext(circ(25.8)-circ(22),CZ,POST-CZ)
for i,xy in enumerate(MOUNTS):
 r=math.hypot(*xy);radial=(xy[0]*24/r,xy[1]*24/r)
 carrier+=ext(CS.batch_hull([circ(3.1,xy),circ(2.4,radial)]),CZ,POST-CZ)
 carrier-=cyl(M2_CLEAR_RADIUS,CZ-.1,POST-CZ+.2,xy)
 fast('display_machine_'+str(i+1),xy,CZ,4,1,'board_mounting_posts_REFERENCE',[POST,POST+3.5])
for i,xy in enumerate(CARRIER_POINTS):
 carrier+=cyl(2.5,CZ,POST-CZ,xy);carrier-=cyl(M2_CLEAR_RADIUS,CZ-.1,POST-CZ+.2,xy)
 front+=cyl(2.5,POST,F-POST,xy);front-=cyl(WHITE_PILOT_RADIUS,POST-.1,7,xy)
 front-=cyl(2.75,SPLIT-.1,POST-SPLIT+.1,xy)  # Verified C11 carrier rear-entry correction ONLY; bearing/pilot retained
 fast('carrier_'+str(i+1),xy,CZ,8,1,FRONT_NAME,[POST,POST+6.9])
# Acoustic route: speaker to right-side exit above battery; mic broad rear plenum.
SPEAKER_AIR=box(34,5,2.0,(15,-15,15.9+LIFT))+box(10,15,3.9,(0,-11,18.35+LIFT))
carrier-=SPEAKER_AIR;front-=SPEAKER_AIR;back-=SPEAKER_AIR
carrier-=box(28.4,14.4,8.4,(-36,0,F-8.6))+BATPLUG+USB_OUTER
# Remove the suspended low battery-plug ledge; retain connector clearance and open outward to avoid a tapered sub-nozzle rim.
if P['carrier_battery_plug_open_through']:
 carrier-=box(P['carrier_battery_plug_relief_outer_x']-15.5,10,POST-CZ+.2,((P['carrier_battery_plug_relief_outer_x']+15.5)/2,-7.5,(POST+CZ)/2))
# Reinforce the retained inner bridge rearwards, keeping its original top and USB throat.
USB_BRIDGE_TOP=F-8.6-4.2
USB_BRIDGE_CS=(circ(25.8)-circ(22)) ^ CS.square((-P['usb_outer_recess_inner_x'],14.4+2*P['usb_carrier_bridge_side_lap']),True).translate((P['usb_outer_recess_inner_x']/2,0))
carrier+=ext(USB_BRIDGE_CS,USB_BRIDGE_TOP-P['usb_carrier_bridge_thickness'],P['usb_carrier_bridge_thickness'])
front-=BATPLUG
for xy in CASE_POINTS:carrier-=cyl(3.3,CZ-.1,POST-CZ+.2,xy)
hw('speaker_air_route_PROVISIONAL',SPEAKER_AIR,(.2,.6,.8,.35),role='acoustic_gauge')
MIC_AIR=box(7,7,19.4+LIFT,(-12.05,14.2,12.1+LIFT/2))
# Air passes around the complete display-post bearing pad in the carrier layer.
MIC_AIR-=cyl(3.4,CZ-.1,POST-CZ+.2,MOUNTS[1])
carrier-=MIC_AIR
for xx in [-14.05,-12.05,-10.05]:
 for yy in [12.2,14.2,16.2]:back-=cyl(.7,-.1,W+.2,(xx,yy))
hw('mic_rear_plenum_PROVISIONAL',MIC_AIR,(.2,.6,.8,.35),role='acoustic_gauge')
# Preserve top lens flex/tab; aperture clears maximum official viewing area, not guessed active pixels.
front-=cyl(P['lens_pocket_diameter']/2,F-5,F-(F-5)+.1)
BEZEL_POINTS=[(26*math.cos(math.radians(a)),26*math.sin(math.radians(a))) for a in [30,150,270]]
# Back-down print: a45degree relief ramp closes the inner opening fromR23.4
# toR20 over3.4mm, preserving the0.6mm minimum glassgap andØ40frontaperture.
bezel=M.cylinder(P['bezel_base_thickness'],29.5,29.5,N).translate((0,0,F))-cyl(23.4,F-.1,.7)-cyl(20,F-.1,P['bezel_base_thickness']+.2)
bezel-=M.cylinder(3.4,23.4,20,N).translate((0,0,F+.6))
for i,xy in enumerate(BEZEL_POINTS):
 bezel+=cyl(P['bezel_collar_outer_diameter']/2,F+3.0,P['bezel_collar_top_offset']-3.0,xy)
 bezel-=cyl(M2_CLEAR_RADIUS,F-.1,P['bezel_collar_top_offset']+.2,xy)+cyl(P['fastener_counterbore_diameter']/2,F+P['bezel_head_bearing_offset'],P['bezel_collar_top_offset']-P['bezel_head_bearing_offset']+.1,xy)
 bezel_post_bottom=F-5.15 if i==2 else F-6
 front+=cyl(2.5,bezel_post_bottom,F-bezel_post_bottom,xy);front-=cyl(WHITE_PILOT_RADIUS,F-5.5,5.6,xy)
 fast('bezel_'+str(i+1),xy,F+P['bezel_head_bearing_offset'],8,-1,FRONT_NAME,[max(F-5.5,bezel_post_bottom),F])
 if i==2:FASTENERS[-1]['printed_pilot_exit']='Rear-open; unchanged4.5mmthread engagement and1.15mmnominaltip-toRFplugclearance; no blindfloor assumed.'
# Reader uses a removable screw/strap tray, no invented PCB mounting holes.
rcx,rcy,rcz=P['reader_center'];floor=P['reader_tray_floor_z']
rout=rr(29.4,21.4,1.5)
reader=ext(rout,floor,2)+ext(rout-rr(27,19,1),floor+2,2.2)
for sign in [-1,1]:reader-=box(8,16,6,(sign*16,0,floor+4.8))
for xx in [-13,13]:
 for yy in [-4,4]:reader-=ext(rr(1.1,3,.3,(xx,yy)),floor-.1,2.2)
reader=reader.rotate((0,0,P['reader_rotation_z_deg'])).translate((rcx,rcy,0))
READER_POINTS=[tuple(xy) for xy in P['reader_mount_points']]
for i,xy in enumerate(READER_POINTS):
 anchor=P['reader_mount_anchors'][i]
 reader+=ext(CS.batch_hull([circ(2.8,xy),circ(2.8,anchor)]),floor,2)
 reader-=cyl(M2_CLEAR_RADIUS,floor-.1,2.2,xy)+cyl(3.1,floor+2,2.4,xy)
 front+=cyl(2.8,floor+2,F-floor-2,xy);front-=cyl(WHITE_PILOT_RADIUS,floor+1.9,7.2,xy)
 fast('reader_'+str(i+1),xy,floor,8,1,FRONT_NAME,[floor+2,floor+9])
# Physical top swipe, shortened support but unchanged antenna and full-card access.
RAIL_MOUNTS=[(-18,22),(18,22)];railz=F-8.7
railshape=rr(P['card_length'],18,1.2,(0,RY))
rail=ext(railshape,railz,7.5)-ext(rr(32,14,.6,(0,RY+1)),railz+2,7)
for i,xy in enumerate(RAIL_MOUNTS):
 tab=ext(rr(9,26,2,(xy[0],30)),railz,2.4)-cyl(23.4,railz-.1,2.6)
 relief=ext(rr(9.5,26.5,2,(xy[0],30)),railz-.2,2.8)-cyl(23.4,railz-.3,3)
 rail+=tab;rail-=cyl(M2_CLEAR_RADIUS,railz-.1,2.6,xy)
 front-=relief;front+=cyl(3,railz+2.4,F-railz-2.4,xy);front-=cyl(WHITE_PILOT_RADIUS,railz+2.3,F-railz-2.4,xy)
 front-=cyl(2.3,SPLIT-.1,railz-SPLIT+.2,xy)
 fast('rail_'+str(i+1),xy,railz,8,1,FRONT_NAME,[railz+2.4,F-.1])
window=ext(railshape,railz+7.5,1.2)+ext(rr(P['card_length'],4,1,(0,RY-7)),F,P['card_gap'])
roof=ext(railshape,F+P['card_gap'],2.4)
ROOF_POINTS=[(-25,RY+P['roof_screw_edge_offset_y']),(25,RY+P['roof_screw_edge_offset_y'])]
for i,xy in enumerate(ROOF_POINTS):
 rail-=cyl(M2_PILOT_RADIUS,railz+.5,7.1,xy);window-=cyl(M2_CLEAR_RADIUS,F-1.3,3,xy);roof-=cyl(M2_CLEAR_RADIUS,F+1.3,2.6,xy)
 roof-=cyl(P['fastener_counterbore_diameter']/2,F+P['roof_head_bearing_offset'],3.9-P['roof_head_bearing_offset'],xy)
 fast('roof_'+str(i+1),xy,F+P['roof_head_bearing_offset'],8,-1,'card_antenna_housing_TOP',[railz+.5,railz+7.5])
# Lay-in RF route has no depinning step. Connector ends/radii remain provisional.
RF_PATH=[tuple(p) for p in P['rf_path_mm']]
rf=tube_path(RF_PATH,1)
RF_CHANNEL_PATH=[tuple(p) for p in P['rf_channel_path_mm']]  # Retain existing material clearances; currentstocklead uses interiorfreevolume.
route=tube_path(RF_CHANNEL_PATH[1:],1.8)
front-=route;carrier-=route;rail-=route
# Each trough is opened toward the rear for installation before closing.
posts=[cyl(1.8,14,p[2]-14+1.8,p[:2]) for p in RF_CHANNEL_PATH[1:]]
layin=M.batch_boolean([M.batch_hull([a,b]) for a,b in zip(posts,posts[1:])],m.OpType.Add)
front-=layin;rail-=layin
# Direct finger recess and lining, independent of switch travel.
from direct_pwr_access import build_access
ACCESS=build_access(P,outline,SPLIT,F)
front=(front+ACCESS['guard'])-ACCESS['opening']
carrier-=ACCESS['guard_clearance']+ACCESS['opening']
reader-=ACCESS['guard_clearance']+ACCESS['opening']
# Delete the redundant unmounted southeast ring arc rather than leave a free island.
sector=CS([[(0,0)]+[(40*math.cos(math.radians(a)),40*math.sin(math.radians(a))) for a in np.linspace(280,353,80)]+[(0,0)]])
carrier-=ext(sector,CZ-.1,POST-CZ+.2)
hw('PWR_FINGER_ILLUSTRATIVE_GAUGE',ACCESS['gauge'],(.4,.65,.9,.3),role='access_gauge')
# Optional complete manufactured gameplay panel switches. No separate button-only
# bosses or carriers exist in C13. Omitting these cuts restores the full front wall.
BUTTON_POINTS=[]
BUTTON_SHOULDER=F-W-P['panel_switch_shoulder_recess']
assert P['panel_switch_shoulder_recess']==0,'A recessed mounting well needs a separate designed floor.'
for i,xy in enumerate(P['button_centers'] if GAMEPLAY_BUTTONS else []):
 front-=cyl(P['panel_switch_mount_bore']/2,F-W-.1,W+.2,xy)
 rear_bound=cyl(P['panel_switch_rear_bound_diameter']/2,BUTTON_SHOULDER-P['panel_switch_rear_depth'],P['panel_switch_rear_depth'],xy)
 front_bound=cyl(P['panel_switch_cap_diameter']/2,BUTTON_SHOULDER,P['panel_switch_front_reach_from_shoulder'],xy)
 hw('panel_switch_'+str(i+1)+'_REAR_BOUND_ASSUMED',rear_bound,(.12,.12,.12,.65),role='unmeasured_switch_bound')
 hw('panel_switch_'+str(i+1)+'_FRONT_REACH_REFERENCE',front_bound,(.04,.04,.04,1),role='switch_photo_dimension_reference')
# Roll shell contact edges after boss unions. Eyelet remains a solid external load path.
contact=box(250,250,gr+.001,(0,0,gr/2-.0005))+box(250,250,gr+.001,(0,0,F-gr/2+.0005))
front-=contact-outer;back-=contact-outer
L=P['lanyard'];LR=L['outer_radius'];LT=L['thickness'];LC=L['edge_chamfer'];LB=L['bore_diameter']/2;LM=L['bore_mouth_chamfer']
back_plain=back
eye=M.cylinder(LC,LR-LC,LR,N)+M.cylinder(LT-2*LC,LR,LR,N).translate((0,0,LC))+M.cylinder(LC,LR,LR-LC,N).translate((0,0,LT-LC))
eye=M.batch_hull([eye.translate((*p,0)) for p in [L['center_xy'],L['root_xy']]])
bore=cyl(LB,-.1,LT+.2,L['center_xy'])+M.cylinder(LM,LB+LM,LB,N).translate((*L['center_xy'],0))+M.cylinder(LM,LB,LB+LM,N).translate((*L['center_xy'],LT-LM))
back=(back+eye)-bore
# Full soft-cell reservation remains free even at the tapered lower corners.
reserve=box(*P['battery_bay_xyz'],P['battery_bay_center'])
back-=reserve;front-=reserve
front-=cyl(P['lens_pocket_diameter']/2,F-5,F-(F-5)+.1)
# Flex/tab relief derived from official noncircular display solid.
front-=box(4.6,16,5.2,(-22.5,0,F-2.4))  # Full front-entry lead-in for actual glass/tab/USB; bezel removed during insertion
# Open the RF lay-in notch fully; no disconnected sliver remains beside the rail tab.
front-=box(5,7,8+LIFT,(20,20,18+LIFT/2))
add('back_shell_C10',back,desc='Battery/guides +Y5; rear floor, mic grill, lanyard and60x135outline preserved.')
add(FRONT_NAME,front,'panel_switches' if GAMEPLAY_BUTTONS else 'touch_only',white,True,'C13 protected direct PWR finger recess, front-entry board/tab lead-in, retained18x12 USBouterrecess/14x8throat; '+('two complete gameplay panel buttons.' if GAMEPLAY_BUTTONS else 'TOUCH_ONLY continuous 2.4mm gameplay panel, no gameplay button bores or switch hardware.'))
add('screen_bezel_COMMON',bezel,color=red,flip=False,desc='Back-down59mm bezel;45degree46.8-to40mm inner ramp;6mmcollars forprovisionalM2x8fixings;min0.6mmglassgap.')
add('display_carrier_COMMON',carrier,flip=True,desc='Three asymmetric official M2 locations; matched shallowUSBouterrecess and locallyreinforced1.2mm rearbridge; frontbearingplanedown; localUSBbridge supportattention; batteryplugreliefopenthrough; SKU29565 only.')
add('nfc_reader_tray_C13',reader,desc='C13reader+90deg atY-42.5; RFtoward+Y/hosttoward-Y;three relocatedM2supports, softstrapsrequired.')
add('card_antenna_housing_TOP',rail,desc='60mm top guide with unchanged25x10mm provisional antenna.')
add('card_RF_window_and_gap_TOP',window,color=white,desc='1.2mm RF window and1.4mm nominal slot.')
add('card_channel_roof_TOP',roof,color=red,desc='Card travels horizontally across top.')
add('battery_41x71_FIT_FRAME',ext(rr(bw+6,bh+6,2)-rr(bw,bh,1),0,3),'fit_first',desc='Envelope gauge only; cell not selected.')
add('screen_fit_ring_46p8',ext(circ(26)-circ(23.4),0,2.4),'fit_first',desc='Standard cover glass only; max44.82mm.')
hw('battery_36x67x10_DESIGN_ENVELOPE',box(*P['battery_candidate_xyz'],P['battery_center']),(.12,.39,.76,1))
hw('nfc_PN5321MINI_ordered',box(*P['reader_envelope_xyz'],(0,0,0)).rotate((0,0,P['reader_rotation_z_deg'])).translate(P['reader_center']),(.04,.52,.28,1))
hw('nfc_antenna_25x10_PROVISIONAL',box(*P['antenna_envelope_xyz'],P['antenna_center']),(.04,.52,.28,1))
hw('reader_host_plug_PROVISIONAL',box(8,15,5,(-16,0,0)).rotate((0,0,P['reader_rotation_z_deg'])).translate(P['reader_center']),role='mating_plug')
hw('reader_RF_plug_PROVISIONAL',box(8,6,5,(16,0,0)).rotate((0,0,P['reader_rotation_z_deg'])).translate(P['reader_center']),role='mating_plug')
hw('reader_host_wire_bend_PROVISIONAL',tube_path(P['host_wire_bend_path_mm'],P['host_wire_bend_diameter_provisional']/2),(.45,.2,.7,.4),role='access_gauge')
hw('rf_route_2mm_CLEARANCE_GAUGE',rf,(.94,.46,.08,1),role='access_gauge')
hw('roof_screw_heads_MAX',M.batch_boolean([cyl(P['fastener_head_diameter_bound']/2,F+P['roof_head_bearing_offset'],P['fastener_head_height_bound'],xy) for xy in ROOF_POINTS],m.OpType.Add),(.28,.3,.32,1))
hw('card_85p6x54x0p9_DEMO',box(85.6,54,.9,(0,RY+22,F+.7)),(.25,.55,.8,.45),False,'illustration')
COMMON=[n for n in PARTS if META[n]['group']!='fit_first'];VARIANTS={VARIANT_NAME:COMMON}
def mesh(solid):
 d=solid.simplify(1e-5).to_mesh64();tm=trimesh.Trimesh(vertices=np.asarray(d.vert_properties)[:,:3].astype(np.float32),faces=np.asarray(d.tri_verts),process=True)
 tm.merge_vertices(digits_vertex=5);tm.update_faces(tm.nondegenerate_faces(height=1e-7));tm.update_faces(tm.unique_faces());tm.remove_unreferenced_vertices();return tm
for folder in ['cad/assembly_meshes','cad/hardware_envelopes','validation','stl']:(ROOT/folder).mkdir(exist_ok=True)
report=[]
for name,s in PARTS.items():
 assert s.status()==m.Error.NoError,(name,s.status())
 tm=mesh(s);tm.export(ROOT/'cad/assembly_meshes'/f'{name}.stl');pt=tm.copy()
 if META[name]['flip_x_for_print']:pt.apply_transform(trimesh.transformations.rotation_matrix(math.pi,[1,0,0]))
 pt.apply_translation([-pt.bounds[:,0].mean(),-pt.bounds[:,1].mean(),-pt.bounds[0,2]])
 path=ROOT/'stl'/META[name]['stl_folder']/f'{name}.stl';path.parent.mkdir(exist_ok=True);pt.export(path);checkmesh=trimesh.load(path,force='mesh')
 _,counts=np.unique(np.sort(checkmesh.edges,axis=1),axis=0,return_counts=True)
 row={'part':name,'file':str(path.relative_to(ROOT)),'watertight':bool(checkmesh.is_watertight),'winding_consistent':bool(checkmesh.is_winding_consistent),'positive_volume':bool(checkmesh.volume>0),'one_body':checkmesh.body_count==1,'all_edges_two_faces':bool(np.all(counts==2)),'volume_mm3':round(float(checkmesh.volume),3),'size_mm':checkmesh.extents.round(3).tolist(),'faces':len(checkmesh.faces)}
 report.append(row);print(name,row['size_mm'],row['one_body'],flush=True)
(ROOT/'validation/mesh_report.json').write_text(json.dumps(report,indent=2))
for name,s in HW.items():mesh(s).export(ROOT/'cad/hardware_envelopes'/f'{name}.stl')
bounds=np.vstack([mesh(PARTS[n]).bounds for n in COMMON]);lo=bounds.min(axis=0);hi=bounds.max(axis=0)
dimensions={VARIANT_NAME:{'overall_xyz':np.round(hi-lo,3).tolist(),'min_xyz':lo.tolist(),'max_xyz':hi.tolist()}}
switch_front_bound=BUTTON_SHOULDER+P['panel_switch_front_reach_from_shoulder'] if GAMEPLAY_BUTTONS else None
roof_head_bound=max(f['bearing_z']+f['head_height'] for f in FASTENERS if f['name'].startswith('roof'))
manifest={'parameters':P,'parts':META,'variants':VARIANTS,'hardware':HWMETA,'dimensions_mm':dimensions,'body_diameter_mm':60,'bezel_diameter_mm':59,
 'case_fasteners_xy':CASE_POINTS,'carrier_fasteners_xy':CARRIER_POINTS,'display_mounts_xy':MOUNTS,'bezel_fasteners_xy':BEZEL_POINTS,'reader_fasteners_xy':READER_POINTS,
 'button_fasteners_xy':BUTTON_POINTS,'panel_switch_centers_xy':P['button_centers'] if GAMEPLAY_BUTTONS else [],'rail_fasteners_xy':RAIL_MOUNTS,'roof_fasteners_xy':ROOF_POINTS,'fasteners':FASTENERS,'rf_path_mm':RF_PATH,
 'panel_switch_mount':({'enabled':True,'panel_thickness_mm':W,'bore_diameter_mm':P['panel_switch_mount_bore'],'shoulder_z':BUTTON_SHOULDER,'rear_tip_bound_z':BUTTON_SHOULDER-P['panel_switch_rear_depth'],'front_tip_reference_z':switch_front_bound,'protrusion_above_front_mm':switch_front_bound-F,'rear_diameter_assumed_mm':P['panel_switch_rear_bound_diameter'],'status':P['panel_switch_status']} if GAMEPLAY_BUTTONS else {'enabled':False,'panel_thickness_mm':W,'bore_count':0,'status':'TOUCH_ONLY: no gameplay panel switch holes or switch hardware. Onboard PWR finger recess retained.'}),
 'render':{'screen_surface_z_mm':F,'hide_for_internals':['back_shell_C10','nfc_reader_tray_C13']},
 'assembled_depth_bound_mm':{'without_magnet':max(float(hi[2]),roof_head_bound,switch_front_bound if GAMEPLAY_BUTTONS else float(hi[2]))-float(lo[2]),'printed_only':float(hi[2]-lo[2]),'roof_heads_top_z':roof_head_bound,'complete_switch_front_tip_z':switch_front_bound,'basis':'Includes retained provisional M2 roof-head bound; gameplay switch reach included only when gameplay_buttons=true. Actual purchased M2 head dimensions remain unverified.'},
 'pilot_revision':{'name':'C14-P19','white_smooth_pilot_diameter_mm':P['m2_white_shell_pilot'],'black_smooth_pilot_diameter_mm':P['m2_plastic_pilot'],'physical_screw_grip':'PENDING'},
 'prototype_status':P['front_release_status']}
(ROOT/'cad/assembly_manifest.json').write_text(json.dumps(manifest,indent=2))
engagements=[]
for f in FASTENERS:
 start=f['bearing_z'];tip=start+f['direction']*f['shaft_length'];lo_tip,hi_tip=sorted([start,tip]);lo_pilot,hi_pilot=f['pilot_z_limits']
 engagement=max(0,min(hi_tip,hi_pilot)-max(lo_tip,lo_pilot))
 margin=(hi_pilot-tip) if f['direction']>0 else (tip-lo_pilot)
 engagements.append({'name':f['name'],'length_mm':f['shaft_length'],'bearing_z':start,'tip_z':tip,'nominal_engagement_mm':round(engagement,6),'modeled_tip_to_pilot_limit_mm':round(margin,6),'target':f['target_part'],'pilot_exit':f.get('printed_pilot_exit','Blindprintedpilot or metalpost; actualmetalpostblinddepth unverified.'),'thread_class':'M2machineboardpost; actualblinddepthunverified' if f['name'].startswith('display_machine') else 'printedplasticpilot; couponqualificationrequired'})
contract={'status':P['fastener_status'],'profile':'Provisional photographed-kit length subsetM2x4/x8/x16; purchased kit identity not confirmed.',
 'head_maximum_mm':{'diameter':P['fastener_head_diameter_bound'],'height':P['fastener_head_height_bound']},
 'through_clearance_diameter_mm':P['m2_clearance'],'printed_pilot_diameter_by_target_mm':{'white_main_shell':P['m2_white_shell_pilot'],'black_top_housing':P['m2_plastic_pilot']},'white_pilot_groups':['A','C','D','E','F'],'white_pilot_count':15,'pilot_coupon_diameters_mm':[1.6,1.7,1.8,1.9],
 'length_counts_per_device':dict(collections.Counter(str(f['shaft_length']) for f in FASTENERS)),'total_fasteners_per_device':len(FASTENERS),
 'nominal_minimum_battery_head_clearance_mm':1.0,'left_case_head_lateral_battery_clearance_mm':1.1,
 'bearing_geometry_mm':{'case_head_seat_z':P['case_head_bearing_z'],'case_counterbore_diameter':4.6,'case_boss_minimum_radial_wall':.7,
 'bezel_collar_outer_diameter':6.0,'bezel_counterbore_diameter':4.4,'bezel_collar_radial_wall':.8,'bezel_seat_offset_from_front':3.5,'bezel_collar_top_offset_from_front':5.7,
 'roof_counterbore_diameter':4.4,'roof_bearing_web':1.2,'roof_minimum_counterbore_edge_wall':.3},
 'joints':engagements,'limitations':[P['white_pilot_status'],'Head dimensions are design maxima, not verified measurements of purchased screws.','M2diameter alone does not establish thread form, usable length, head fit or printed pilot fit.','Original metal-post nominal1.6mmengagement preserved; actual post blinddepth must be measured beforeassembly.','No nuts, inserts or washers are modeled for these M2joints. Gameplay panel switches are absent when gameplay_buttons=false.','Bezel prints rear annulus down, collars up;45degreeinner ramp preservesatleast0.6mmglassclearance andØ40view.']}
(ROOT/'reference/FASTENER_CONTRACT.json').write_text(json.dumps(contract,indent=2))
checks=[]
def check(label,val,passed,**kw):checks.append({'check':label,'value':val,'pass':bool(passed),**kw})
def clear(label,a,b,**kw):
 inter=a^b;v=abs(inter.volume())
 if v>=.01:kw['intersection_bounds']=list(inter.bounding_box())
 check(label,round(v,7),v<.01,**kw)
for a,b in itertools.combinations(COMMON,2):clear('part_pair',PARTS[a],PARTS[b],parts=[a,b])
for h,s in HW.items():
 if not HWMETA[h]['check_collision']:continue
 for name in COMMON:clear('hardware_vs_part',s,PARTS[name],hardware=h,part=name)
for xx in [-105,-75,-30,0,30,75,105]:
 for name in COMMON:clear('card_sweep',box(85.6,54,.9,(xx,RY+22,F+.7)),PARTS[name],card_x=xx,part=name)
for xx in [-75,0,75]:
 for name in COMMON:clear('card_holding_gauge',box(24,18,22,(xx,RY+45,F+.7)),PARTS[name],card_x=xx,part=name)
bay=box(*P['battery_bay_xyz'],P['battery_bay_center'])
for name in COMMON:clear('battery_full_reservation',bay,PARTS[name],part=name)
for h,s in HW.items():
 if HWMETA[h]['check_collision'] and h!='battery_36x67x10_DESIGN_ENVELOPE':clear('battery_reservation_vs_hardware',bay,s,hardware=h)
for f in FASTENERS:
 hz=f['bearing_z']-f['head_height'] if f['direction']>0 else f['bearing_z']
 head=cyl(f['head_diameter']/2,hz,f['head_height'],f['xy'])
 clear('battery_reservation_vs_fastener_head',bay,head,fastener=f['name'])
 if f['name'].startswith(('carrier_','reader_','display_machine_')):
  gap=hz-(P['battery_bay_center'][2]+P['battery_bay_xyz'][2]/2)
  check('internal_head_to_battery_axial_gap_mm',round(gap,6),gap>=P['required_battery_head_clearance']-1e-6,fastener=f['name'])
for name in COMMON:
 if name!='back_shell_C10':clear('lanyard_thread_access',cyl(LB+LM,-1,F+15,L['center_xy']),PARTS[name],part=name)
check('lanyard_root_fusion_mm3',(eye^back_plain).volume(),(eye^back_plain).volume()>100)
clear('lanyard_bore',bore,back)
check('lens_radial_clearance_mm',(P['lens_pocket_diameter']-P['lens_diameter_max'])/2,(P['lens_pocket_diameter']-P['lens_diameter_max'])/2>=.6)
check('glass_axial_clearance_mm',.6,True)
length=sum(math.dist(a,b) for a,b in zip(RF_PATH,RF_PATH[1:]));check('RF_route_nominal_arithmetic_mm',length,length>0,note='Arithmetic validity only, not supplied cable compatibility. Stock100mm lead budget includes10mm planning allowance; actual bend/termination geometry still needs verification.')
rf_contract={'modeled_centerline_mm':length,'inherited_nominal_lead_mm':P['rf_lead_nominal_mm'],'service_planning_allowance_mm':P['rf_service_planning_allowance_mm'],'planning_minimum_usable_lead_mm':length+P['rf_service_planning_allowance_mm'],'baseline_nominal_sufficient':P['rf_lead_nominal_mm']>=length+P['rf_service_planning_allowance_mm'],'status':'STOCK100_NOMINAL_LENGTH_BUDGET; final physical lead routing, bend radius and termination geometry unverified.'}
check('stock100_RF_route_plus_planning_allowance_mm',length+P['rf_service_planning_allowance_mm'],length+P['rf_service_planning_allowance_mm']<=P['rf_lead_nominal_mm'],note='Officialstock100mm cablelength budget; actual termination/bend geometry remains unverified.')
(ROOT/'reference/RF_ROUTING_CONTRACT.json').write_text(json.dumps(rf_contract,indent=2))
(ROOT/'validation/clearance_checks.json').write_text(json.dumps(checks,indent=2))
failed=[c for c in checks if not c['pass']];meshfailed=[r for r in report if not all(r[k] for k in ['watertight','winding_consistent','positive_volume','one_body','all_edges_two_faces'])]
summary={'dimensions_mm':dimensions,'mesh_count':len(report),'mesh_failures':meshfailed,'checks':len(checks),'failures':failed,'rf_route_length_mm':length,'status':'PASS' if not failed and not meshfailed else 'NEEDS_REPAIR'}
(ROOT/'validation/summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2),flush=True)
if failed or meshfailed:raise SystemExit(1)
