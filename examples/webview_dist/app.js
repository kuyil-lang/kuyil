function testGreet() {
    console.log('testGreet called');
    var el = document.getElementById('result');
    el.innerHTML = "<div class='output'>Calling greet...</div>";
    
    window.kuyil.call('greet', 'World').then(function(r) {
        console.log('Promise resolved with:', r);
        el.innerHTML += "<div class='output success'>✅ " + r + "</div>";
    }).catch(function(e) {
        console.error('Promise rejected:', e);
        el.innerHTML += "<div class='output'>❌ Error: " + e + "</div>";
    });
}

function testCalculate() {
    var el = document.getElementById('result');
    el.innerHTML = "<div class='output'>Calculating...</div>";
    
    window.kuyil.call('calculate', 'dummy').then(function(r) {
        el.innerHTML += "<div class='output success'>✅ Result: " + r + "</div>";
    }).catch(function(e) {
        el.innerHTML += "<div class='output'>❌ Error: " + e + "</div>";
    });
}

function testProcess() {
    var el = document.getElementById('result');
    el.innerHTML = "<div class='output'>Processing...</div>";
    
    window.kuyil.call('processOrder', 'dummy').then(function(r) {
        el.innerHTML += "<div class='output success'>✅ " + r + "</div>";
    }).catch(function(e) {
        el.innerHTML += "<div class='output'>❌ Error: " + e + "</div>";
    });
}

// Auto-test on load
setTimeout(testGreet, 1000);
