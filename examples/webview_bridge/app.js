(function(){
  const logEl = document.getElementById('log');
  const incomingEl = document.getElementById('incoming');
  const inputEl = document.getElementById('msgInput');
  const sendBtn = document.getElementById('sendBtn');

  function log(msg){
    const ts = new Date().toISOString().split('T')[1].replace('Z','');
    logEl.textContent += `[${ts}] ${msg}\n`;
    logEl.scrollTop = logEl.scrollHeight;
  }

  async function sendToKuyil(){
    const msg = inputEl.value.trim();
    if(!msg) return;
    try {
      const res = await fetch('/api/push', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: msg
      });
      const text = await res.text();
      log(`Sent → Kuyil: ${msg} (status ${res.status})`);
      inputEl.value = '';
    } catch(err){
      log('Error sending to Kuyil: ' + err);
    }
  }

  async function pollFromKuyil(){
    try {
      const res = await fetch('/api/pull');
      const data = await res.json().catch(()=>({message:null}));
      if (data && data.message){
        incomingEl.textContent = data.message;
        log(`Recv ← Kuyil: ${data.message}`);
      }
    } catch(err){
      // Silent on polling errors to avoid log spam
    } finally {
      setTimeout(pollFromKuyil, 1000);
    }
  }

  sendBtn.addEventListener('click', sendToKuyil);
  inputEl.addEventListener('keydown', (e)=>{ if(e.key==='Enter') sendToKuyil(); });

  // Kick off
  pollFromKuyil();
  log('Bridge initialized.');
})();
