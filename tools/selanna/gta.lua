function isspace(c)
	return c == ' ' or c == '\t'
end

function split(line)
    local fields = {}
    local i = 1
    local len = #line

    while i <= len do
        while i <= len and isspace(line:sub(i, i)) do
            i = i + 1
        end
        if i > len then break end

        if line:sub(i, i) == '"' then
            i = i + 1
            local field = {}
            while i <= len do
                local c = line:sub(i, i)
                if c == '"' then
                    i = i + 1
                    break
                end
                field[#field + 1] = c
                i = i + 1
            end
            fields[#fields + 1] = table.concat(field)
        else
            local start = i
            while i <= len and not isspace(line:sub(i, i)) do
                i = i + 1
            end
            fields[#fields + 1] = line:sub(start, i - 1)
        end
    end
    return fields
end

TokenStream = {}
TokenStream.__index = TokenStream

function TokenStream:make(line)
	local fields = split(line)
	local t = { fields = fields, i = 1 }
	setmetatable(t, TokenStream)
	return t
end

function TokenStream:next(line)
	local f = self.fields[self.i]
	self.i = self.i + 1
	return f
end

function TokenStream:nextInt(line)
	return tonumber(self:next())
end

function TokenStream:nextFloat(line)
	return tonumber(self:next())
end

function TokenStream:nextXYZ(line)
	local x = self:nextFloat()
	local y = self:nextFloat()
	local z = self:nextFloat()
	return { x = x, y = y, z = z }
end

function TokenStream:nextXYZW(line)
	local x = self:nextFloat()
	local y = self:nextFloat()
	local z = self:nextFloat()
	local w = self:nextFloat()
	return { x = x, y = y, z = z, w = w }
end

function TokenStream:nextRGBA(line)
	local r = self:nextFloat()
	local g = self:nextFloat()
	local b = self:nextFloat()
	local a = self:nextFloat()
	return { r = r, g = g, b = b, a = a }
end

function addString(line, s)
	if line == "" then
		return s
	else
		return line .. ", " .. s
	end
end
function addInt(line, i)
	return addString(line, tostring(i))
end
function addHex(line, i)
	return addString(line, string.format("%x", (i)))
end
function addFloat(line, f)
	return addString(line, tostring(f))
end
function addXYZ(line, xyz)
	line = addFloat(line, xyz.x)
	line = addFloat(line, xyz.y)
	return addFloat(line, xyz.z)
end
function addXYZW(line, xyzw)
	line = addFloat(line, xyzw.x)
	line = addFloat(line, xyzw.y)
	line = addFloat(line, xyzw.z)
	return addFloat(line, xyzw.w)
end
function addRGBA(line, rgba)
	line = addFloat(line, rgba.r)
	line = addFloat(line, rgba.g)
	line = addFloat(line, rgba.b)
	return addFloat(line, rgba.a)
end


gta = {}
gta.__index = gta

gta.GameIII = 0
gta.GameVC = 1
gta.GameSA = 2

-- model types
gta.MT_BUILDING = 1
gta.MT_TIMEDBUILDING = 2
gta.MT_CLUMP = 3
gta.MT_PED = 4
gta.MT_VEHICLE = 5
gta.MT_WEAPON = 6

function gta.make(game, gameDir)
	local g = {}
	setmetatable(g, gta)
	g.game = game
	g.gameDir = gameDir
	g.files = {}

	g.streamedFiles = {}
	g.modelsById = {}
	g.modelsByName = {}
	g.buildings = {}
	g.timedBuildings = {}
	g.clumps = {}
	g.peds = {}
	g.vehicles = {}
	g.weapons = {}
	g.effects = {}

	g.instances = {}
	g.cullZones = {}
	g.zones = {}
	g.mapZones = {}
	g.occluders = {}

	g.pathSegments = {}
	return g
end

function gta:GetFileFs(file)
	file = file:gsub("\\", "/")
	if self.files[file] then
		return self.files[file]
	end
	local path = rw.makePath(self.gameDir .. "/" .. file)
	local f = { fileName = file, fullPath = path, modified = false }
	self.files[file] = f
	return f
end

function gta:SwitchGameDir(dir)
	self.gameDir = dir
	for i, f in pairs(self.files) do
		f.fullPath = rw.makePath(self.gameDir .. "/" .. f.fileName)
	end
end

function gta:MkDir(dir)
	os.execute("mkdir -p " .. dir)
end

function gta:PopulateGameDir()
	local dirs = {}
	for _, f in pairs(self.files) do
		f.fileName = f.fileName:lower()
		f.fullPath = rw.makePath(self.gameDir .. "/" .. f.fileName)
		local dir = f.fullPath:match("^(.*)/[^/]*$")
		if dir and dir ~= "" then
			dirs[dir] = true
		end
	end
	for dir in pairs(dirs) do
		gta:MkDir(dir)
	end
end

function gta:AddModel(obj, tab, type)
	obj.sourceFile = self.currentFile
	obj.type = type
	table.insert(tab, obj)
	self.modelsById[obj.id] = obj
	self.modelsByName[obj.model:lower()] = obj
end

function gta:AddBuilding(obj)
	self:AddModel(obj, self.buildings, gta.MT_BUILDING)
end

function gta:AddTimedBuilding(obj)
	self:AddModel(obj, self.timedBuildings, gta.MT_TIMEDBUILDING)
end

function gta:AddClump(obj)
	self:AddModel(obj, self.clumps, gta.MT_CLUMP)
end

function gta:AddPed(obj)
	self:AddModel(obj, self.peds, gta.MT_PED)
end

function gta:AddVehicle(obj)
	self:AddModel(obj, self.vehicles, gta.MT_VEHICLE)
end

function gta:AddWeapon(obj)
	self:AddModel(obj, self.weapons, gta.MT_WEAPON)
end

function gta:AddEffect(e)
	e.sourceFile = self.currentFile
	table.insert(self.effects, e)
end

function gta:AddInstance(inst)
	inst.sourceFile = self.currentFile
	table.insert(self.instances, inst)
end

function gta:AddCullZone(zone)
	zone.sourceFile = self.currentFile
	table.insert(self.cullZones, zone)
end

function gta:AddZone(zone)
	zone.sourceFile = self.currentFile
	table.insert(self.zones, zone)
end

function gta:AddMapZone(zone)
	zone.sourceFile = self.currentFile
	table.insert(self.mapZones, zone)
end

function gta:AddOccluder(occl)
	occl.sourceFile = self.currentFile
	table.insert(self.occluders, occl)
end

function gta:AddPathSegment(seg)
	local obj = self.modelsById[seg.id]
	seg.sourceFile = self.currentFile
	table.insert(self.pathSegments, seg)
end

function gta:LinkStreamingInfo()
	for _, s in pairs(self.streamedFiles) do
		if s.ext == "DFF" then
			local mdl = self.modelsByName[s.name:lower()]
			if mdl then
				print("model", mdl.model)
				mdl.streamingInfo = s
			end
		end
	end
end

function gta:GetModelClump(mdl)
	if mdl.clump then return mdl.clump end
	local si = mdl.streamingInfo
	if not si or si.ext ~= "DFF" then return nil end
	mdl.clump = rw.readClumpFile(si.container.fullPath, si.offset*2048, si.size*2048)
	return mdl.clump
end

-- file loading

function gta:ReadObjLine(line)
--	print("obj ", line)
	local t = TokenStream:make(line)
	local obj = {}
	obj.id = t:nextInt()
	obj.model = t:next()
	obj.texDict = t:next()
	obj.numAtomics = t:nextInt()
	for i = 1,obj.numAtomics do
		obj["lodDist" .. i] = t:nextFloat()
	end
	obj.flags = t:nextInt()
	self:AddBuilding(obj)
end

function gta:WriteObjLine(obj)
	local line = ""
	line = addInt(line, obj.id)
	line = addString(line, obj.model)
	line = addString(line, obj.texDict)
	line = addInt(line, obj.numAtomics)
	for i = 1,obj.numAtomics do
		line = addFloat(line, obj["lodDist" .. i])
	end
	line = addInt(line, obj.flags)
	return line
end


function gta:ReadTObjLine(line)
--	print("tobj ", line)
	local t = TokenStream:make(line)
	local obj = {}
	obj.id = t:nextInt()
	obj.model = t:next()
	obj.texDict = t:next()
	obj.numAtomics = t:nextInt()
	for i = 1,obj.numAtomics do
		obj["lodDist" .. i] = t:nextFloat()
	end
	obj.flags = t:nextInt()
	obj.timeOn = t:nextInt()
	obj.timeOff = t:nextInt()
	self:AddTimedBuilding(obj)
end

function gta:WriteTObjLine(obj)
	local line = ""
	line = addInt(line, obj.id)
	line = addString(line, obj.model)
	line = addString(line, obj.texDict)
	line = addInt(line, obj.numAtomics)
	for i = 1,obj.numAtomics do
		line = addFloat(line, obj["lodDist" .. i])
	end
	line = addInt(line, obj.flags)
	line = addInt(line, obj.timeOn)
	line = addInt(line, obj.timeOff)
	return line
end


function gta:ReadHierLine(line)
--	print("hier ", line)
	local t = TokenStream:make(line)
	local clp = {}
	clp.id = t:nextInt()
	clp.model = t:next()
	clp.texDict = t:next()
	self:AddClump(clp)
end

function gta:WriteHierLine(clp)
	local line = ""
	line = addInt(line, clp.id)
	line = addString(line, clp.model)
	line = addString(line, clp.texDict)
	return line
end


function gta:ReadPedLine(line)
--	print("peds ", line)
	local t = TokenStream:make(line)
	local ped = {}
	ped.id = t:nextInt()
	ped.model = t:next()
	ped.texDict = t:next()
	ped.pedType = t:next()
	ped.pedStats = t:next()
	ped.animGroup = t:next()
	ped.carMask = tonumber("0x" .. t:next())
	if self.game > gta.GameIII then
		ped.animFile = t:next()
		ped.radio1 = t:nextInt()
		ped.radio2 = t:nextInt()
	end
	self:AddPed(ped)
end

function gta:WritePedLine(ped)
	local line = ""
	line = addInt(line, ped.id)
	line = addString(line, ped.model)
	line = addString(line, ped.texDict)
	line = addString(line, ped.pedType)
	line = addString(line, ped.pedStats)
	line = addString(line, ped.animGroup)
	line = addHex(line, ped.carMask)
	if self.game > gta.GameIII then
		line = addString(line, ped.animFile)
		line = addInt(line, ped.radio1)
		line = addInt(line, ped.radio2)
	end
	return line
end

function gta:ReadVehicleLine(line)
--	print("cars ", line)
	local t = TokenStream:make(line)
	local veh = {}
	veh.id = t:nextInt()
	veh.model = t:next()
	veh.texDict = t:next()
	veh.vehicleType = t:next()
	veh.handlingName = t:next()
	veh.gameName = t:next()
	if self.game > gta.GameIII then
		veh.animFile = t:next()
	end
	veh.rating = t:next()
	veh.freq = t:nextInt()
	veh.levels = t:nextInt()
	veh.compRules = tonumber("0x" .. t:next())
	if veh.vehicleType == "car" then
		veh.wheelId = t:nextInt()
		veh.wheelScale = t:nextFloat()
	elseif veh.vehicleType == "plane" then
		veh.planeLodId = t:nextInt()
	elseif veh.vehicleType == "bike" then
		veh.bikeSteerAngle = t:nextInt()
		veh.wheelScale = t:nextFloat()
	end
	self:AddVehicle(veh)
end

function gta:WriteVehicleLine(veh)
	local line = ""
	line = addInt(line, veh.id)
	line = addString(line, veh.model)
	line = addString(line, veh.texDict)
	line = addString(line, veh.vehicleType)
	line = addString(line, veh.handlingName)
	line = addString(line, veh.gameName)
	if self.game > gta.GameIII then
		line = addString(line, veh.animFile)
	end
	line = addString(line, veh.rating)
	line = addInt(line, veh.freq)
	line = addInt(line, veh.levels)
	line = addHex(line, veh.compRules)
	if veh.vehicleType == "car" then
		line = addInt(line, veh.wheelId)
		line = addFloat(line, veh.wheelScale)
	elseif veh.vehicleType == "plane" then
		line = addInt(line, veh.planeLodId)
	elseif veh.vehicleType == "bike" then
		line = addInt(line, veh.bikeSteerAngle)
		line = addFloat(line, veh.wheelScale)
	end
	return line
end

function gta:ReadWeaponLine(line)
--	print("weap ", line)
	local t = TokenStream:make(line)
	local weap = {}
	weap.id = t:nextInt()
	weap.model = t:next()
	weap.texDict = t:next()
	weap.animFile = t:next()
	t:next()	-- unused - 1
	weap.lodDist = t:nextFloat()
	self:AddWeapon(weap)
end

function gta:WriteWeaponLine(weap)
	local line = ""
	line = addInt(line, weap.id)
	line = addString(line, weap.model)
	line = addString(line, weap.texDict)
	line = addString(line, weap.animFile)
	line = addInt(line, 1)
	line = addFloat(line, weap.loadDist)
	return line
end

function gta:ReadPathLine(line)
	local t = TokenStream:make(line)

	if not self._pathSegment then
		self._pathSegment = { }
		local seg = self._pathSegment;
		if self.game == gta.GameIII then
			if t:next() == "ped" then
				seg.type = 0
			else
				seg.type = 1
			end
		else
			seg.type = t:nextInt()
		end
		seg.id = t:nextInt()
		if seg.id >= 0 then
			seg.name = t:next()
		end
		seg.nodes = {}
	else
		local node = {}
		node.type = t:nextInt()
		node.link = t:nextInt()
		node.cross = t:nextInt()
		node.pos = t:nextXYZ()
		node.width = t:nextFloat()
		node.lanesIn = t:nextInt()
		node.lanesOut = t:nextInt()
		if self.game > gta.GameIII then
			node.speed = t:nextInt()
			node.flags = t:nextInt()
			node.density = t:nextFloat()
		end
		if self.game == gta.GameSA then
			node.special = t:nextInt()
		end

		local seg = self._pathSegment
		table.insert(seg.nodes, node)
		if #seg.nodes == 12 then
			self:AddPathSegment(seg)
			self._pathSegment = nil
		end
	end
end

local pathtypestr = { "ped", "car" }

function gta:WritePathNodeLine(node)
	local line = ""
	line = addInt(line, node.type)
	line = addInt(line, node.link)
	line = addInt(line, node.cross)
	line = addXYZ(line, node.pos)
	line = addFloat(line, node.width)
	line = addInt(line, node.lanesIn)
	line = addInt(line, node.lanesOut)
	if self.game > gta.GameIII then
		line = addInt(line, node.speed)
		line = addFloat(line, node.flags)
		line = addFloat(line, node.density)
	end
	if self.game == gta.GameSA then
		line = addInt(line, node.special)
	end
	return "\t" .. line
end

function gta:WritePathSegment(seg)
	local line = ""
	if self.game == gta.GameIII then
		line = addString(line, pathtypestr[seg.type+1])
	else
		line = addInt(line, seg.type)
	end
	line = addInt(line, seg.id)
	if seg.name then
		line = addString(line, seg.name)
	end
	for i = 1,12 do
		line = line .. "\n"
		line = line .. self:WritePathNodeLine(seg.nodes[i])
	end
	return line
end

local FX_LIGHT = 0
local FX_PARTICLE = 1
local FX_LOOKATPOINT = 2
local FX_PEDQUEUE = 3
local FX_SUNGLARE = 4

function gta:Read2dfxLine(line)
	local t = TokenStream:make(line)

	local e = {}
	e.id = t:nextInt()
	e.position = t:nextXYZ(t)
	e.color = t:nextRGBA(t)
	e.type = t:nextInt()

	if e.type == FX_LIGHT then
		e.coronaTex = t:next()
		e.shadowTex = t:next()
		e.lodDist = t:nextFloat()
		e.size = t:nextFloat()
		e.coronaSize = t:nextFloat()
		e.shadowSize = t:nextFloat()
		e.shadowAlpha = t:nextInt()
		e.flashiness = t:nextInt()
		e.reflection = t:nextInt()
		e.lensFlareType = t:nextInt()
		e.flags = t:nextInt()
	elseif e.type == FX_PARTICLE then
		e.particleType = t:nextInt()
		e.dir = t:nextXYZ()
		e.size = t:nextFloat()
	elseif e.type == FX_LOOKATPOINT then
		e.lookType = t:nextInt()
		e.dir = t:nextXYZ()
		e.probability = t:nextInt()
	elseif e.type == FX_PEDQUEUE then
		e.queueType = t:nextInt()
		e.queueDir = t:nextXYZ()
		e.useDir = t:nextXYZ()
	end
	self:AddEffect(e)
end

function gta:Write2dfxLine(e)
	local line = ""
	line = addInt(line, e.id)
	line = addXYZ(line, e.position)
	line = addRGBA(line, e.color)
	line = addInt(line, e.type)

	if e.type == FX_LIGHT then
		line = addString(line, '"' .. e.coronaTex .. '"')
		line = addString(line, '"' .. e.shadowTex .. '"')
		line = addFloat(line, e.lodDist);
		line = addFloat(line, e.size);
		line = addFloat(line, e.coronaSize);
		line = addFloat(line, e.shadowSize);
		line = addInt(line, e.shadowAlpha);
		line = addInt(line, e.flashiness);
		line = addInt(line, e.reflection);
		line = addInt(line, e.lensFlareType);
		line = addInt(line, e.flags);
	elseif e.type == FX_PARTICLE then
		line = addInt(line, e.particleType);
		line = addXYZ(line, e.dir);
		line = addFloat(line, e.size);
	elseif e.type == FX_LOOKATPOINT then
		line = addInt(line, e.lookType);
		line = addXYZ(line, e.dir);
		line = addInt(line, e.probability);
	elseif e.type == FX_PEDQUEUE then
		line = addInt(line, e.queueType);
		line = addXYZ(line, e.queueDir);
		line = addXYZ(line, e.useDir);
	end
	return line
end

local IDEdesc = {}
IDEdesc["objs"] = gta.ReadObjLine
IDEdesc["tobj"] = gta.ReadTObjLine
IDEdesc["hier"] = gta.ReadHierLine
IDEdesc["peds"] = gta.ReadPedLine
IDEdesc["cars"] = gta.ReadVehicleLine
IDEdesc["weap"] = gta.ReadWeaponLine
IDEdesc["path"] = gta.ReadPathLine
IDEdesc["2dfx"] = gta.Read2dfxLine



function gta:ReadInstLine(line)
	local t = TokenStream:make(line)

	local inst = {}
	inst.id = t:nextInt()
	inst.name = t:next()
	if self.game > gta.GameIII then
		inst.area = t:nextInt()
	end
	inst.position = t:nextXYZ()
	inst.scale = t:nextXYZ()
	inst.rotation = t:nextXYZW()
	self:AddInstance(inst)
end

function gta:WriteInstLine(inst)
	local line = ""
	line = addInt(line, inst.id)
	line = addString(line, inst.name)
	if self.game > gta.GameIII then
		line = addInt(line, inst.area)
	end
	line = addXYZ(line, inst.position)
	line = addXYZ(line, inst.scale)
	line = addXYZW(line, inst.rotation)
	return line
end

function gta:ReadCullLine(line)
	local t = TokenStream:make(line)
	local cull = {}
	cull.pos = t:nextXYZ()
	cull.min = t:nextXYZ()
	cull.max = t:nextXYZ()
	cull.flags = t:nextInt()
	cull.wantedLevelDrop = t:nextInt()
	self:AddCullZone(cull)
end

function gta:WriteCullLine(cull)
	local line = ""
	line = addXYZ(line, cull.pos)
	line = addXYZ(line, cull.min)
	line = addXYZ(line, cull.max)
	line = addInt(line, cull.flags)
	line = addInt(line, cull.wantedLevelDrop)
	return line
end

function gta:ReadZoneLine(line)
	local t = TokenStream:make(line)
	local zone = {}
	zone.name = t:next()
	zone.type = t:nextInt()
	zone.min = t:nextXYZ()
	zone.max = t:nextXYZ()
	zone.level = t:nextInt()
	self:AddZone(zone)
end

function gta:ReadMapZoneLine(line)
	local t = TokenStream:make(line)
	local zone = {}
	zone.name = t:next()
	zone.type = t:nextInt()
	zone.min = t:nextXYZ()
	zone.max = t:nextXYZ()
	zone.level = t:nextInt()
	self:AddMapZone(zone)
end

function gta:WriteZoneLine(zone)
	local line = ""
	line = addString(line, zone.name)
	line = addInt(line, zone.type)
	line = addXYZ(line, zone.min)
	line = addXYZ(line, zone.max)
	line = addInt(line, zone.level)
	return line
end

function gta:ReadOccluderLine(line)
	local t = TokenStream:make(line)
	local occl = {}
	occl.pos = t:nextXYZ()
	occl.dimension = t:nextXYZ()
	occl.angle = t:nextFloat()
	self:AddOccluder(occl)
end

function gta:WriteOccluderLine(occl)
	local line = ""
	line = addXYZ(line, occl.pos)
	line = addXYZ(line, occl.dimension)
	line = addFloat(line, occl.angle)
	return line
end

local IPLdesc = {}
IPLdesc["inst"] = gta.ReadInstLine
IPLdesc["cull"] = gta.ReadCullLine
IPLdesc["zone"] = gta.ReadZoneLine
-- VC
IPLdesc["path"] = gta.ReadPathLine
IPLdesc["occl"] = gta.ReadOccluderLine

local ZONdesc = {}
ZONdesc["zone"] = gta.ReadMapZoneLine


function gta:ReadFileByDesc(file, desc)
	self.currentFile = self:GetFileFs(file);
	local f = io.open(self.currentFile.fullPath, "r")
	if not f then
		print("Could not open file: " .. filename)
		return
	end
	local sect = 'end'
	self._pathSegment = nil
	for line in f:lines() do
		line = line:gsub(","," "):gsub("#.*", ""):gsub("\r","")
		if line == "" then goto continue end
		local fields = split(line)
		if sect == 'end' then
			sect = fields[1]
		elseif fields[1] == 'end' then
			sect = 'end'
		elseif desc[sect] then
			desc[sect](self, line)
		else
			print('unknown', sect, line, desc[sect])
		end
		::continue::
	end
	f:close()
end

function gta:ReadCdImage(imgfile)
	local dirfile = string.sub(imgfile, 0, -4) .. "dir"
	self.currentFile = self:GetFileFs(dirfile);
	local imgfile = self:GetFileFs(imgfile);
	local f = io.open(self.currentFile.fullPath, "rb")
	if not f then error("couldn't open " .. dirpath) end
	while true do
		local data = f:read(32)
		if not data or #data < 32 then break end
		local item = {}
		item.offset, item.size = string.unpack("<I4I4", data)
		item.file = data:sub(9):match("^([^%z]+)")
		item.name = item.file:match("[^.]+")
		item.ext = item.file:sub(string.len(item.name)+2):upper()
		item.container = imgfile
		item.sourceFile = self.currentFile
		table.insert(self.streamedFiles, item)
	end
	f:close()
end

function gta:ReadDataFile(file)
	self.currentFile = self:GetFileFs(file);
	local f = io.open(self.currentFile.fullPath, "r")
	if not f then
		print("Could not open file: " .. filename)
		return
	end

	for line in f:lines() do
		line = line:gsub("#.*", ""):gsub("\r","")
		if line == "" then goto continue end
		local fields = split(line)

--		print(line)
		if fields[1] == "IDE" then
			self:ReadFileByDesc(fields[2], IDEdesc)
		elseif fields[1] == "IPL" then
			self:ReadFileByDesc(fields[2], IPLdesc)
		elseif fields[1] == "MAPZONE" then
			self:ReadFileByDesc(fields[2], ZONdesc)
		elseif fields[1] == "COLFILE" then
--			print("COL FILE", fields[2], fields[3])
			self.currentFile = self:GetFileFs(fields[3]);
		elseif fields[1] == "CDIMAGE" then
			self:ReadCdImage(fields[2])
		elseif fields[1] == "SPLASH" then
			self.currentFile = {}
		elseif fields[1] == "MODELFILE" then
			self.currentFile = self:GetFileFs(fields[2]);
-- TODO
		elseif fields[1] == "TEXDICTION" then
			self.currentFile = self:GetFileFs(fields[2]);
-- TODO
		else
print("unknown", fields[1])
			self.currentFile = {}
		end
		self.currentFile.type = fields[1]
		::continue::
	end
	f:close()
end

function gta:WriteIDE(file)
	local f = io.open(file.fullPath, "w")
	if not f then error("couldn't open " .. file.fullPath) end

	f:write("peds\n")
	for _, obj in pairs(self.peds) do
		if obj.sourceFile == file then
			f:write(self:WritePedLine(obj) .. "\n")
		end
	end
	f:write("end\n")

	f:write("cars\n")
	for _, obj in pairs(self.vehicles) do
		if obj.sourceFile == file then
			f:write(self:WriteVehicleLine(obj) .. "\n")
		end
	end
	f:write("end\n")

	f:write("objs\n")
	for _, obj in pairs(self.buildings) do
		if obj.sourceFile == file then
			f:write(self:WriteObjLine(obj) .. "\n")
		end
	end
	f:write("end\n")

	f:write("tobj\n")
	for _, obj in pairs(self.timedBuildings) do
		if obj.sourceFile == file then
			f:write(self:WriteTObjLine(obj) .. "\n")
		end
	end
	f:write("end\n")

	f:write("path\n")
	for _, seg in pairs(self.pathSegments) do
		if seg.sourceFile == file then
			f:write(self:WritePathSegment(seg) .. "\n")
		end
	end
	-- TODO
	f:write("end\n")

	f:write("2dfx\n")
	for _, seg in pairs(self.effects) do
		if seg.sourceFile == file then
			f:write(self:Write2dfxLine(seg) .. "\n")
		end
	end
	f:write("end\n")

	f:write("weap\n")
	for _, obj in pairs(self.weapons) do
		if obj.sourceFile == file then
			f:write(self:WriteWeaponLine(obj) .. "\n")
		end
	end
	f:write("end\n")

	f:write("hier\n")
	for _, obj in pairs(self.clumps) do
		if obj.sourceFile == file then
			f:write(self:WriteHierLine(obj) .. "\n")
		end
	end
	f:write("end\n")

	f:close()
end

function gta:WriteIPL(file)
	local f = io.open(file.fullPath, "w")
	if not f then error("couldn't open " .. file.fullPath) end

	f:write("inst\n")
	for _, inst in pairs(self.instances) do
		if inst.sourceFile == file then
			f:write(self:WriteInstLine(inst) .. "\n")
		end
	end
	f:write("end\n")

	f:write("cull\n")
	for _, inst in pairs(self.cullZones) do
		if inst.sourceFile == file then
			f:write(self:WriteCullLine(inst) .. "\n")
		end
	end
	f:write("end\n")

	f:write("zone\n")
	for _, inst in pairs(self.zones) do
		if inst.sourceFile == file then
			f:write(self:WriteZoneLine(inst) .. "\n")
		end
	end
	f:write("end\n")

	f:write("path\n")
	for _, seg in pairs(self.pathSegments) do
		if seg.sourceFile == file then
			f:write(self:WritePathSegment(seg) .. "\n")
		end
	end
	f:write("end\n")

	f:write("occl\n")
	for _, inst in pairs(self.occluders) do
		if inst.sourceFile == file then
			f:write(self:WriteOccluderLine(inst) .. "\n")
		end
	end
	f:write("end\n")

	f:close()
end

function gta:WriteZON(file)
	local f = io.open(file.fullPath, "w")
	if not f then error("couldn't open " .. file.fullPath) end

	f:write("zone\n")
	for _, inst in pairs(self.mapZones) do
		if inst.sourceFile == file then
			f:write(self:WriteZoneLine(inst) .. "\n")
		end
	end
	f:write("end\n")

	f:close()
end


function gta.readDir(dirpath)
	local entries = {}
	local f = io.open(dirpath, "rb")
	if not f then error("couldn't open " .. dirpath) end
	while true do
		local data = f:read(32)
		if not data or #data < 32 then break end

		local offset, size = string.unpack("<I4I4", data)
		-- trim null bytes from filename
		local name = data:sub(9):match("^([^%z]+)")

		 entries[name:lower()] = {
			 offset = offset,
			 size   = size,
			 name   = name,
		 }
	end
	f:close()
	return entries
end

function gta.printDir(dir)
	for name, entry in pairs(dir) do
		print(string.format("%-32s  offset: %6d  size: %d", name, entry.offset, entry.size))
	end
end

