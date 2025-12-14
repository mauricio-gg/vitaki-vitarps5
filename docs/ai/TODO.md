# TODO

## Focus Manager & Navigation
- [ ] Test D-pad navigation in all content screens to verify no regression

## Input & Controller
- [ ] L2/R2 trigger mapping (`vita/src/host.c:209`)
- [ ] Home button handling with long hold (`vita/src/host.c:210`)
- [ ] Fully configurable controller mapping (`vita/src/controller.c:6`)

## Power & Configuration
- [ ] Power control configuration (`vita/src/main.c:149`)
- [ ] Input handling configuration (`vita/src/main.c:151`)
- [ ] Stack size optimization review (`vita/src/main.c:77`)

## UI & Graphics
- [ ] Manual host deletion from UI (`vita/src/ui.c:1058`)
- [ ] Connection abort functionality during connection attempt (`vita/src/ui.c:2555`)
- [ ] Profile screen scrolling support (`vita/src/ui.c:2465`)
- [ ] Console icon tinting optimization (reduce memory usage)
- [ ] Wave background animation future enhancement
- [ ] Touch-based nav bar pill expansion visual indicator refinement

## Network & Discovery
- [ ] Discovery error user feedback indication (`vita/src/discovery.c:59`)
- [ ] Manual host limit separate constant from MAX_NUM_HOSTS

## Performance & Optimization
- [ ] Latency optimization across streaming pipeline (HIGH PRIORITY)
- [ ] Video decoder optimization
- [ ] Audio buffer tuning
- [ ] Network protocol efficiency
- [ ] Frame pacing improvements
- [ ] Input lag reduction

## Future / Placeholder
- [ ] PSN profile picture actual retrieval (currently placeholder)
- [ ] Motion controls full implementation (currently stub)
- [ ] Settings features marked "Coming Soon"
