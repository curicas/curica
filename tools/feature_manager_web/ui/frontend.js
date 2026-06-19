document.addEventListener('DOMContentLoaded', function() {
    var currentState = 'proposed';
    var allFeatures = [];
    var currentFeature = null;

    var navLinks = document.querySelectorAll('.nav-links li');
    var featureGrid = document.getElementById('feature-grid');
    var detailView = document.getElementById('feature-detail-view');
    var btnBack = document.getElementById('btn-back');
    var statusTitle = document.getElementById('status-title');
    var categoryList = document.getElementById('category-list');
    var categorySearch = document.getElementById('category-search');
    
    var activeCategories = [];
    var currentAvailableCategories = [];

    categorySearch.addEventListener('input', function(e) {
        renderCategoryFilters(e.target.value);
    });

    function loadFeatures() {
        fetch('/api/features')
            .then(function(res) { return res.json(); })
            .then(function(data) {
                allFeatures = data;
                updateAvailableCategories();
                renderCategoryFilters();
                renderGrid();
            })
            .catch(function(err) { console.error('Failed to load features', err); });
    }

    function updateAvailableCategories() {
        var cats = {};
        for (var i = 0; i < allFeatures.length; i++) {
            if (allFeatures[i].status === currentState) {
                cats[allFeatures[i].category] = true;
            }
        }
        currentAvailableCategories = Object.keys(cats).sort();
        // Keep active categories that are still available
        activeCategories = activeCategories.filter(function(c) {
            return currentAvailableCategories.indexOf(c) > -1;
        });
    }

    function renderCategoryFilters(searchQuery) {
        categoryList.innerHTML = '';
        var query = (searchQuery || '').toLowerCase();
        
        for (var i = 0; i < currentAvailableCategories.length; i++) {
            var cat = currentAvailableCategories[i];
            if (query && cat.toLowerCase().indexOf(query) === -1) continue;
            
            var label = document.createElement('label');
            label.className = 'category-item';
            
            var checkbox = document.createElement('input');
            checkbox.type = 'checkbox';
            checkbox.value = cat;
            checkbox.checked = activeCategories.indexOf(cat) > -1;
            
            checkbox.onchange = function(e) {
                var val = e.target.value;
                if (e.target.checked) {
                    if (activeCategories.indexOf(val) === -1) activeCategories.push(val);
                } else {
                    var idx = activeCategories.indexOf(val);
                    if (idx > -1) activeCategories.splice(idx, 1);
                }
                renderGrid();
            };
            
            label.appendChild(checkbox);
            label.appendChild(document.createTextNode(' ' + cat));
            categoryList.appendChild(label);
        }
    }

    function renderGrid() {
        featureGrid.innerHTML = '';
        var filtered = allFeatures.filter(function(f) { 
            var matchStatus = f.status === currentState;
            var matchCat = activeCategories.length === 0 || activeCategories.indexOf(f.category) > -1;
            return matchStatus && matchCat;
        });
        
        if (filtered.length === 0) {
            featureGrid.innerHTML = '<p>No features found in this state.</p>';
            return;
        }

        for (var i = 0; i < filtered.length; i++) {
            var f = filtered[i];
            var card = document.createElement('div');
            card.className = 'feature-card';
            
            var tagsHtml = '';
            var keys = Object.keys(f.metadata);
            for (var j = 0; j < keys.length; j++) {
                tagsHtml += '<span class="tag">' + keys[j] + ': ' + f.metadata[keys[j]] + '</span>';
            }
            
            card.innerHTML = 
                '<div class="card-category">' + f.category + '</div>' +
                '<h3 class="card-title">' + f.name.replace(/_/g, ' ') + '</h3>' +
                '<div class="feature-tags">' + tagsHtml + '</div>';
            
            (function(feature) {
                card.onclick = function() { openDetail(feature); };
            })(f);
            
            featureGrid.appendChild(card);
        }
    }

    function openDetail(feature) {
        currentFeature = feature;
        featureGrid.classList.add('hidden');
        detailView.classList.remove('hidden');
        
        document.getElementById('detail-title').innerText = feature.name.replace(/_/g, ' ');
        
        var tagsContainer = document.getElementById('detail-tags');
        tagsContainer.innerHTML = '';
        var keys = Object.keys(feature.metadata);
        for (var j = 0; j < keys.length; j++) {
            tagsContainer.innerHTML += '<span class="tag">' + keys[j] + ': ' + feature.metadata[keys[j]] + '</span>';
        }

        var actions = document.getElementById('detail-actions');
        actions.innerHTML = '';
        if (feature.status === 'proposed') {
            var btn = document.createElement('button');
            btn.className = 'primary-btn';
            btn.innerText = 'Approve Feature';
            btn.onclick = function() { moveFeature('approved'); };
            actions.appendChild(btn);
        } else if (feature.status === 'approved') {
            var btn2 = document.createElement('button');
            btn2.className = 'success-btn';
            btn2.innerText = 'Mark as Completed';
            btn2.onclick = function() { moveFeature('completed'); };
            actions.appendChild(btn2);
        }
        
        document.getElementById('detail-content').innerText = 'Loading...';
        fetch('/api/feature_content?path=' + encodeURIComponent(feature.path))
            .then(function(res) { return res.text(); })
            .then(function(text) {
                var meta = extractMetadata(text);
                tagsContainer.innerHTML = '';
                var keys = Object.keys(meta);
                for (var j = 0; j < keys.length; j++) {
                    tagsContainer.innerHTML += '<span class="tag">' + keys[j] + ': ' + meta[keys[j]] + '</span>';
                }
                
                document.getElementById('detail-content').innerText = text;
            });
    }

    function extractMetadata(content) {
        var meta = {};
        var lines = content.split('\n');
        for (var i = 0; i < lines.length; i++) {
            var line = lines[i];
            if (line.indexOf("**") === 0) {
                var colon = line.indexOf(":");
                if (colon > -1) {
                    var kStr = line.substring(2, colon);
                    if (kStr.indexOf("**") > -1) kStr = kStr.substring(0, kStr.indexOf("**"));
                    var key = kStr.trim();
                    var val = line.substring(colon + 1).trim();
                    meta[key] = val;
                }
            }
        }
        return meta;
    }

    function closeDetail() {
        currentFeature = null;
        detailView.classList.add('hidden');
        featureGrid.classList.remove('hidden');
        document.getElementById('term-output').innerText = 'Ready.';
    }

    function moveFeature(newStatus) {
        if (!currentFeature) return;
        fetch('/api/move', {
            method: 'POST',
            body: JSON.stringify({
                path: currentFeature.path,
                category: currentFeature.category,
                file: currentFeature.file,
                status: newStatus
            })
        })
        .then(function() {
            closeDetail();
            loadFeatures();
        });
    }

    btnBack.onclick = closeDetail;

    for (var i = 0; i < navLinks.length; i++) {
        navLinks[i].onclick = function(e) {
            for (var j = 0; j < navLinks.length; j++) navLinks[j].classList.remove('active');
            var target = e.currentTarget;
            target.classList.add('active');
            currentState = target.getAttribute('data-status');
            statusTitle.innerText = target.innerText.trim() + ' Features';
            closeDetail();
            updateAvailableCategories();
            renderCategoryFilters(categorySearch.value);
            renderGrid();
        };
    }

    document.getElementById('btn-execute').onclick = function() {
        var cmd = document.getElementById('term-input').value;
        if (!cmd) return;
        var out = document.getElementById('term-output');
        out.innerText = 'Executing...';
        fetch('/api/execute', {
            method: 'POST',
            body: JSON.stringify({ command: cmd })
        })
        .then(function(res) { return res.json(); })
        .then(function(data) {
            if (data.error) out.innerText = 'ERROR:\n' + data.error;
            else out.innerText = data.output;
        })
        .catch(function(err) {
            out.innerText = 'Failed: ' + err;
        });
    };

    loadFeatures();
});
