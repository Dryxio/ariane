#!/usr/bin/env node
//
// gen_categories.js
// Reads mta_objects.xml and mta_vehicles.xml, generates object_categories.h
//

const fs = require('fs');
const path = require('path');

const dir = __dirname;
const objectsXml = fs.readFileSync(path.join(dir, 'mta_objects.xml'), 'utf-8');
const vehiclesXml = fs.readFileSync(path.join(dir, 'mta_vehicles.xml'), 'utf-8');

// Categories: { name, parent } indexed by category index
const categories = [];
// Mapping: modelId -> categoryIndex (first occurrence wins)
const modelMap = new Map();

function escapeStr(s) {
	return s.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
}

function parseXml(xml, isVehicles) {
	const lines = xml.split('\n');
	// Stack of group category indices
	const groupStack = [];

	// For vehicles, we wrap everything in a top-level "Vehicles" category
	let vehiclesTopIdx = -1;
	if (isVehicles) {
		vehiclesTopIdx = categories.length;
		categories.push({ name: 'Vehicles', parent: -1 });
	}

	for (const line of lines) {
		// Check for <group name="...">
		const groupMatch = line.match(/<group\s+name="([^"]*)"/);
		if (groupMatch) {
			const name = groupMatch[1];
			let parentIdx;
			if (groupStack.length === 0) {
				if (isVehicles) {
					// Top-level vehicle groups are children of "Vehicles"
					parentIdx = vehiclesTopIdx;
				} else {
					parentIdx = -1;
				}
			} else {
				parentIdx = groupStack[groupStack.length - 1];
			}
			const idx = categories.length;
			categories.push({ name: name, parent: parentIdx });
			groupStack.push(idx);
			continue;
		}

		// Check for </group>
		if (/<\/group>/.test(line)) {
			groupStack.pop();
			continue;
		}

		// Check for <object ...> or <vehicle ...>
		const objMatch = line.match(/<(?:object|vehicle)\s+model="(\d+)"/);
		if (objMatch) {
			const modelId = parseInt(objMatch[1], 10);
			// Assign to the innermost group
			if (groupStack.length > 0 && !modelMap.has(modelId)) {
				modelMap.set(modelId, groupStack[groupStack.length - 1]);
			}
			continue;
		}
	}
}

// Parse objects first, then vehicles
parseXml(objectsXml, false);
parseXml(vehiclesXml, true);

// Build sorted model->category array
const entries = Array.from(modelMap.entries());
entries.sort((a, b) => a[0] - b[0]);

// Generate header
let out = '';
out += '// Auto-generated from MTA:SA Map Editor catalog files\n';
out += '// Categories for GTA SA object browser\n';
out += '\n';
out += '#define NUM_OBJ_CATEGORIES ' + categories.length + '\n';
out += '\n';
out += 'struct ObjCategory {\n';
out += '\tconst char *name;\n';
out += '\tint parent;  // -1 for top-level\n';
out += '};\n';
out += '\n';
out += 'static ObjCategory objCategories[] = {\n';
for (let i = 0; i < categories.length; i++) {
	const c = categories[i];
	out += '\t{ "' + escapeStr(c.name) + '", ' + c.parent + ' },\n';
}
out += '};\n';
out += '\n';
out += 'static int objCategoryMap[][2] = {\n';
for (const [modelId, catIdx] of entries) {
	out += '\t{ ' + modelId + ', ' + catIdx + ' },\n';
}
out += '\t{ -1, -1 }\n';
out += '};\n';

const outPath = path.join(dir, 'object_categories.h');
fs.writeFileSync(outPath, out, 'utf-8');
console.log('Generated ' + outPath);
console.log('  ' + categories.length + ' categories, ' + entries.length + ' model mappings');
