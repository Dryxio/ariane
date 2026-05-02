function readModel(model, dir, imgfile)
	local name = model .. ".dff"
	local entry = dir[name:lower()]
	if not entry then
		return nil
	end
	return rw.readClumpFile(imgfile, entry.offset*2048, entry.size*2048)
end

rw.init()

function printHierarchy(frame, indent)
	indent = indent or 0
	print(string.rep("  ", indent) .. frame:getName() .. "  " .. frame:count())
	for child in frame:children() do
		printHierarchy(child, indent + 1)
	end
end

--local dirfile = "/u/aap/gta/gta3_re/models/gta3.dir"
--local imgfile = "/u/aap/gta/gta3_re/models/gta3.img"
--local dirfile = "/u/aap/gta/gta_miami/models/gta3.dir"
--local imgfile = "/u/aap/gta/gta_miami/models/gta3.img"

--local dir = gta.readDir(dirfile)
--gta.printDir(dir)
--local clump = readModel("player", dir, imgfile)
--for a in clump:atomics() do
--	local g = a:getGeometry()
--	print("atomic ", a:getFrame():getName())
--	print(" geo", g:getNumVertices(), g:getNumTriangles())
--end
--local root = clump:getFrame()
--printHierarchy(root)
--clump:writeFile("out.dff")

function printtab(t, indent)
	indent = indent or 2
	local pref = string.rep("  ", indent)
	for k,v in pairs(t)  do
		if type(v)=="table" then
			print(pref .. k)
			printtab(v, indent+1)
		else 
			print(pref .. k,v)
		end
	end      
end

function LoadIII()
	local g = gta.make(gta.GameIII, "/u/aap/gta/gta3_re")
	g:ReadCdImage("models/gta3.img")
	g:ReadDataFile("data/default.dat")
	g:ReadDataFile("data/gta3.dat")
	g:LinkStreamingInfo()
	return g
end

function LoadVC()
	local g = gta.make(gta.GameVC, "/u/aap/gta/gta_miami")
	g:ReadCdImage("models/gta3.img")
	g:ReadDataFile("data/default.dat")
	g:ReadDataFile("data/gta_vc.dat")
	g:LinkStreamingInfo()
	return g
end

function LoadLCS()
	local g = gta.make(gta.GameVC, "/u/aap/other/gta/gta_lcs")
	g:ReadCdImage("models/gta3.img")
	g:ReadDataFile("data/gta_vc.dat")
	g:LinkStreamingInfo()
	return g
end

function copyGame(g, dir)
	g:SwitchGameDir(dir)
	g:PopulateGameDir()
	for _, f in pairs(g.files) do
		if f.type == 'IDE' then
			g:WriteIDE(f)
		elseif f.type == 'IPL' then
			g:WriteIPL(f)
		elseif f.type == 'MAPZONE' then
			g:WriteZON(f)
		else
			print("not copying", f.type, f.fileName)
		end
	end
end


--local iii = LoadIII()
--copyGame(iii, "/u/aap/gta/test3")
--local mdl = iii.modelsByName['kuruma']
--for _, f in pairs(iii.streamedFiles) do
--	printtab(f)
--end

local vc = LoadVC()
--copyGame(vc, "/u/aap/gta/testvc")
local mdl = vc.modelsByName['admiral']

local clump = gta:GetModelClump(mdl)
for a in clump:atomics() do
	local g = a:getGeometry()
	print("atomic ", a:getFrame():getName())
	print(" geo", g:getNumVertices(), g:getNumTriangles())
	for m in g:materials() do
		local t = m:getTexture()
		local fx = m:getMatFX()
		print("  material", m:getColor(), m:getSurfaceProps())
		if t then
			print("      tex", t:getName(), t:getMaskName())
		end
		if fx then
			print("	matfx", fx:getEnvTexture(), fx:getEnvCoefficient())
		end
	end
end
local root = clump:getFrame()
printHierarchy(root)

--local lcs = LoadLCS()
--copyGame(lcs, "/u/aap/gta/testlcs")

--for i, b in pairs(vc.peds) do
--	print(i)
--	printtab(b)
--end


--local lcs = LoadLCS()
--for i, b in pairs(lcs.peds) do
--	print(i)
--	printtab(b)
--end
--print(#g.buildings)
