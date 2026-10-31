// "New motifs": every key gets a fresh phrase. Motifs come from the Motif set
// number, so this just moves to the next set (and a set you liked is always
// one number away).
export function register(sk) {
  sk.widget("newMotifs", (el, ctx) => {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "btn";
    btn.textContent = "New motifs";
    btn.title = "Give every key a new phrase (moves to the next motif set)";
    btn.addEventListener("click", () => {
      const p = ctx.param("phrase.set");
      const next = ctx.get("phrase.set") >= p.max ? p.min : ctx.get("phrase.set") + 1;
      ctx.set("phrase.set", next);
    });
    el.append(btn);
  });
}
