 $(function() {

     // uniqify an array
     Array.prototype.unique =
         function() {
             var a = [];
             var l = this.length;
             for(var i=0; i<l; i++) {
                 for(var j=i+1; j<l; j++) {
                     // If this[i] is found later in the array
                     if (this[i] === this[j])
                         j = ++i;
                 }
                 a.push(this[i]);
             }
             return a;
         };

     $.ajax({
         url: "trace.txt",
         context: document.body,
         dataType: "text",
         success: function(text) {

             lines = text.split('\n')

             // Parse lines starting with "Evaluating" or "Realizing"
             evals = lines.filter(function(line) {
                 return (line.substring(0, 10) == "Evaluating" ||
                         line.substring(0, 9) == "Realizing");
             })             

             // Find lines of interest
             evals = evals.map(function(line) {
                 var pieces = line.split(' ');
                 var name = pieces[1].replace('.', '_');
                 var coords = pieces.slice(3);

                 coords = coords.map(function(c) {
                     return parseInt(c.split(',')[0])
                 })

                 return {type:pieces[0], name:name, coords:coords};
             })

             // Detect unique functions
             var functions = evals.map(function(e) {return e.name;}).unique();

             // Make the canvas for each
             surfaces = {};             
             functions.map(function(f) {
                 // Detect dimensionality (Assume 2 for now)

                 // Detect bounds (Assume 100 for now)                          

                 // Make a canvas
                 $('#main').append('<canvas id="' + f + '" width=300 height=300></canvas>');

                 alert(f);

                 // And a context
                 var surface = $('#' + f)[0].getContext("2d");
                 surface.strokeRect(0, 0, 300, 300);
                 surfaces[f] = surface;
             });

             // Make the function that will draw our events
             drawEvent = function(event) {
                 if (event.type == "Evaluating") {
                     surfaces[event.name].fillStyle = "rgba(0, 0, 200, 1)";
                     surfaces[event.name].fillRect(event.coords[0]*3, event.coords[1]*3, 3, 3);  
                 } else if (event.type == "Realizing") {
                     surfaces[event.name].fillStyle = "rgba(255, 255, 255, 1)";
                     surfaces[event.name].fillRect(1, 1, 298, 298);
                     surfaces[event.name].strokeRect(event.coords[0]*3, event.coords[2]*3, event.coords[1]*3, event.coords[3]*3); 
                 }
             };

             var count = 0;

             loop = function() {
                 if (evals.length < count) return;
                 drawEvent(evals[count]);
                 count++;
                 t = setTimeout("loop()", 10);
             }

             loop();

         }
     });
 });