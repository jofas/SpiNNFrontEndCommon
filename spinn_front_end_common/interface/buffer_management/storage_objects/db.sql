-- A table describing the regions of every core.
CREATE TABLE IF NOT EXISTS regions(
    global_region_id INTEGER PRIMARY KEY AUTOINCREMENT,
	x INTEGER NOT NULL,
	y INTEGER NOT NULL,
	processor INTEGER NOT NULL,
	region INTEGER NOT NULL);
-- Every processor's regions have a unique ID
CREATE UNIQUE INDEX IF NOT EXISTS regionSanity ON regions(
	x ASC, y ASC, processor ASC, region ASC);

-- A table mapping unique names to blobs of data. It's trivial!
CREATE TABLE IF NOT EXISTS storage(
	storage_id INTEGER PRIMARY KEY AUTOINCREMENT,
	global_region_id INTEGER UNIQUE NOT NULL,
	content BLOB,
    run INTEGER NOT NULL DEFAULT 1,
	FOREIGN KEY(global_region_id) REFERENCES regions(global_region_id));

-- A table describing the regions of every core.
CREATE TABLE IF NOT EXISTS region_locations(
    location_id INTEGER PRIMARY KEY AUTOINCREMENT,
	global_region_id INTEGER UNIQUE NOT NULL,
    address INTEGER NOT NULL,
    size INTEGER NOT NULL,
    run INTEGER NOT NULL DEFAULT 1,
	FOREIGN KEY(global_region_id) REFERENCES regions(global_region_id));
