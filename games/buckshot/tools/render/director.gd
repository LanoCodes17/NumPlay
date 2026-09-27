extends SceneTree
# Render director for the Buckshot art pipeline.
#
# Loads a scene of the original game's Godot project, poses it (animation
# players seeked to fixed times, nodes shown or hidden, items placed in the
# table slots, camera placed) and saves one PNG per shot, rendered at the
# NumWorks screen size with the game's own post-processing.
#
# Run from the game project: Godot --path <project> -s director.gd
# with SHOTS_FILE=<json> in the environment (written by tools/render.py).

var main
var cam
var cfg
var shots = []
var idx = -1
var wait = 0
var anims = {}
var spawned = []
var saved = {}  # node -> [visible, position, rotation] before any shot touched it
var cull0 = 0   # the camera's own cull mask
const MASK_LAYER = 20

func touch(n):
	if n and not saved.has(n):
		var v = [n.get("visible"), null, null, {}]
		if n is Node3D:
			v[1] = n.position
			v[2] = n.rotation_degrees
		saved[n] = v
	return n

func collect(n):
	if n is AnimationPlayer:
		anims[str(main.get_path_to(n))] = n
		n.callback_mode_process = AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_MANUAL
		# poses only: no method calls (they would run the game's logic) and no sound
		for an in n.get_animation_list():
			var a = n.get_animation(an)
			for t in a.get_track_count():
				var ty = a.track_get_type(t)
				if ty == Animation.TYPE_METHOD or ty == Animation.TYPE_AUDIO or ty == Animation.TYPE_ANIMATION:
					a.track_set_enabled(t, false)
	for c in n.get_children():
		collect(c)

func quiet(n):
	if n is GPUParticles3D or n is CPUParticles3D:
		n.emitting = false
		n.visible = false
	for c in n.get_children():
		quiet(c)

func _initialize():
	cfg = JSON.parse_string(FileAccess.get_file_as_string(OS.get_environment("SHOTS_FILE")))
	shots = cfg["shots"]
	var sz = cfg.get("size", [320, 240])
	root.content_scale_mode = Window.CONTENT_SCALE_MODE_DISABLED
	DisplayServer.window_set_size(Vector2i(sz[0], sz[1]))
	root.size = Vector2i(sz[0], sz[1])
	load_scene()
	for n in ["DebugTools", "ModLoader"]:
		var d = root.get_node_or_null(n)
		if d:
			d.queue_free()
	wait = int(cfg.get("warmup", 90))

# (Re)creates the scene: with "fresh", every shot starts from a new instance,
# since some animations leave state behind that no RESET track undoes.
func load_scene():
	if main:
		root.remove_child(main)
		main.free()
	anims = {}
	saved = {}
	spawned = []
	main = load(cfg.get("scene", "res://scenes/main.tscn")).instantiate()
	root.add_child(main)
	cam = main.get_node_or_null(cfg.get("camera", "Camera"))
	if cam: cull0 = cam.cull_mask
	white = StandardMaterial3D.new()
	white.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	white.albedo_color = Color(1, 1, 1)
	collect(main)
	for p in cfg.get("disable", []):
		var n = main.get_node_or_null(p)
		if n:
			n.process_mode = Node.PROCESS_MODE_DISABLED

func node(p):
	var n = main.get_node_or_null(p)
	if n == null:
		print("MISSING ", p)
	return touch(n)

func apply(s):
	for key in ["hide", "show", "hide2", "show2"]:
		for p in s.get(key, []):
			touch(main.get_node_or_null(p))
	for key in ["xform", "props", "layers", "rebase"]:
		for t in s.get(key, []):
			touch(main.get_node_or_null(t[0]))
	if s.get("reset", true):
		for k in anims:
			var ap = anims[k]
			if ap.has_animation("RESET"):
				ap.play("RESET")
				ap.seek(0, true)
				ap.advance(0)
			ap.stop(true)
		for it in spawned:
			it.queue_free()
		spawned = []
		for n in saved:
			if saved[n][0] != null: n.visible = saved[n][0]
			if n is Node3D:
				n.position = saved[n][1]
				n.rotation_degrees = saved[n][2]
			for k in saved[n][3]:
				if k is String:
					n.set_indexed(NodePath(k), saved[n][3][k])
				else:
					n.set_layer_mask_value(k, saved[n][3][k])
	quiet(main)
	if cam: cam.cull_mask = cull0
	for a in s.get("anims", []):
		var ap = anims.get(a[0])
		if ap == null:
			print("NOANIM ", a[0])
			continue
		ap.play(a[1])
		ap.seek(float(a[2]), true)
		ap.advance(0)
	for p in s.get("hide", []):
		var n = node(p)
		if n: n.visible = false
	for p in s.get("show", []):
		var n = node(p)
		if n: n.visible = true
	# "2" lists: nodes the animations above may have toggled, set last
	for p in s.get("hide2", []):
		var n = node(p)
		if n: n.visible = false
	for p in s.get("show2", []):
		var n = node(p)
		if n: n.visible = true
	var im = main.get_node_or_null("standalone managers/item manager")
	for it in s.get("items", []):
		var arr = im.instanceArray if it[1] == "p" else im.instanceArray_dealer
		var grids = im.gridParentArray if it[1] == "p" else im.gridParentArray_enemy
		var res = null
		for r in arr:
			if r.itemName == it[0]:
				res = r
		if res == null:
			print("NOITEM ", it[0])
			continue
		var inst = res.instance.instantiate()
		im.itemSpawnParent.add_child(inst)
		if it.size() > 3:
			inst.transform.origin = Vector3(it[3][0], it[3][1], it[3][2])
			inst.rotation_degrees = Vector3(it[4][0], it[4][1], it[4][2])
		else:
			var g = grids[int(it[2])]
			inst.transform.origin = g.transform.origin + res.pos_offset
			inst.rotation_degrees = g.rotation_degrees + res.rot_offset
		spawned.append(inst)
	var sp = main.get_node_or_null("standalone managers/shell spawner")
	for sh in s.get("shells", []):
		var inst = sp.shellInstance.instantiate()
		var br = inst.get_child(0)
		br.isLive = sh[0] == "live"
		sp.spawnParent.add_child(inst)
		br.ApplyStatus()
		inst.transform.origin = sp.shellLocationArray[int(sh[1])]
		inst.rotation_degrees = Vector3(-90, -90, 180)
		spawned.append(inst)
	if s.get("ejectshell") != null:
		var em = main.get_node(s["ejectshell"][0])
		touch(em.mesh)
		em.mesh.set_surface_override_material(1, em.mat_live if s["ejectshell"][1] == "live" else em.mat_blank)
		em.mesh.visible = true
	if s.get("examine") != null:
		var ex = main.get_node("standalone managers/shell examine")
		touch(ex.shellParent)
		ex.mesh.set_surface_override_material(1, ex.mat_live if s["examine"] == "live" else ex.mat_blank)
		ex.shellParent.visible = true
	for sh in s.get("shells_at", []):
		var inst = sp.shellInstance.instantiate()
		var br = inst.get_child(0)
		br.isLive = sh[0] == "live"
		main.add_child(inst)
		br.ApplyStatus()
		inst.global_position = Vector3(sh[1][0], sh[1][1], sh[1][2])
		inst.rotation_degrees = Vector3(sh[2][0], sh[2][1], sh[2][2])
		if sh.size() > 3: inst.scale = Vector3(sh[3], sh[3], sh[3])
		spawned.append(inst)
	for l in s.get("lights", []):
		var o = OmniLight3D.new()
		main.add_child(o)
		o.global_position = Vector3(l[0][0], l[0][1], l[0][2])
		o.light_energy = l[1]
		o.omni_range = l[2]
		spawned.append(o)
	for t in s.get("xform", []):
		var n = node(t[0])
		if n == null:
			continue
		if t[1] != null: n.position = Vector3(t[1][0], t[1][1], t[1][2])
		if t[2] != null: n.rotation_degrees = Vector3(t[2][0], t[2][1], t[2][2])
	for t in s.get("resources", []):
		# [resource path, property, resource path of the value]: kept for the rest of the run
		var r = load(t[0])
		r.set(t[1], load(t[2]))
	for t in s.get("layers", []):
		var n = node(t[0])
		if n:
			if not saved[n][3].has(int(t[1])): saved[n][3][int(t[1])] = n.get_layer_mask_value(int(t[1]))
			n.set_layer_mask_value(int(t[1]), bool(t[2]))
	for t in s.get("props", []):
		var n = node(t[0])
		if n:
			if not saved[n][3].has(t[1]): saved[n][3][t[1]] = n.get(t[1])
			n.set(t[1], t[2])
	for t in s.get("env", []):
		# [property, value] of the scene's environment (the ending's grading)
		var we = main.get_node_or_null("WorldEnvironment")
		if we:
			touch(we)
			if not saved[we][3].has("environment:" + t[0]): saved[we][3]["environment:" + t[0]] = we.environment.get(t[0])
			we.environment.set(t[0], t[1])
	if s.has("socket"):
		var cm = main.get_node("standalone managers/camera manager")
		for so in cm.socketArray:
			if so.socketName == s["socket"]:
				cam.position = so.pos
				cam.rotation_degrees = so.rot
				cam.fov = so.fov
	if s.has("campos"): cam.position = Vector3(s["campos"][0], s["campos"][1], s["campos"][2])
	if s.has("camrot"): cam.rotation_degrees = Vector3(s["camrot"][0], s["camrot"][1], s["camrot"][2])
	if s.has("fov"): cam.fov = s["fov"]
	for t in s.get("rebase", []):
		rebase(t[0], t[1])
	# the mask pass: only the posed objects, plain white on a layer of their
	# own, so that tools/cut.py keeps their pixels and nothing else (no
	# shadows or light they cast on the table)
	if s.get("maskpass", false):
		var roots = []
		for p in s.get("maskroots", []):
			var n = main.get_node_or_null(p)
			if n: roots.append(n)
			else: print("MISSING ", p)
		if s.get("examine") != null:
			roots.append(main.get_node("standalone managers/shell examine").shellParent)
		if s.get("ejectshell") != null:
			roots.append(main.get_node(s["ejectshell"][0]).mesh)
		roots.append_array(spawned)
		for r in roots:
			mask_layer(r)
		cam.cull_mask = int(s.get("maskcull", 1 << (MASK_LAYER - 1)))

var white = null  # the mask pass draws the posed objects plain white
func mask_layer(n):
	if n is VisualInstance3D:
		touch(n)
		if not saved[n][3].has(MASK_LAYER): saved[n][3][MASK_LAYER] = n.get_layer_mask_value(MASK_LAYER)
		n.set_layer_mask_value(MASK_LAYER, true)
		if n is GeometryInstance3D:
			if not saved[n][3].has("material_override"): saved[n][3]["material_override"] = n.material_override
			n.material_override = white
	for c in n.get_children():
		mask_layer(c)

# Moves a node so that it sits in front of the current camera the way it sat
# in front of the camera at `socket` (keeps first-person poses in frame).
func rebase(p, socket):
	var n = node(p)
	var cm = main.get_node("standalone managers/camera manager")
	for so in cm.socketArray:
		if so.socketName == socket:
			var r = so.rot * PI / 180.0
			var from = Transform3D(Basis.from_euler(r, EULER_ORDER_YXZ), so.pos)
			var to = cam.transform
			if cam.get_parent() is Node3D:
				to = cam.get_parent().global_transform * to
				from = cam.get_parent().global_transform * from
			n.global_transform = to * from.affine_inverse() * n.global_transform

func _process(_delta):
	if wait > 0:
		wait -= 1
		return false
	if idx >= 0:
		var img = root.get_texture().get_image()
		img.save_png(cfg["out"] + "/" + shots[idx]["name"] + ".png")
	idx += 1
	if idx >= shots.size():
		quit()
		return false
	if cfg.get("fresh", false) and idx > 0:
		load_scene()
	apply(shots[idx])
	wait = int(shots[idx].get("wait", cfg.get("wait", 4)))
	return false
