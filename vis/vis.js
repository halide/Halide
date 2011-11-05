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

     debug = function(line) {
         $('#scratch').val($('#scratch').val() + line + '\n');         
     }
     $('#scratch').val('');

     $.ajax({
         url: "trace.txt",
         context: document.body,
         dataType: "text",
         success: function(text) {

             lines = text.split('\n')

             // Parse lines starting with "Evaluating" or "Realizing"
             evals = lines.filter(function(line) {
                 return line.substring(0, 3) == "###";
             })             

             // Find lines of interest
             evals = evals.map(function(line) {
                 var pieces = line.split(' ');
                 var name = pieces[2].replace('.', '_');
                 var coords = pieces.slice(4);

                 //debug(pieces[1] + ', ' + name + ', ' + coords[0] + '**' + coords[1]); 

                 coords = coords.map(function(l) {return parseInt(l);});

                 return {type:pieces[1], name:name, coords:coords};

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

                 // And a context
                 var surface = $('#' + f)[0].getContext("2d");
                 surface.strokeStyle = "rgba(0, 0, 0, 1)";
                 surface.strokeRect(0, 0, 300, 300);
                 surface.lineWidth = 2;
                 surface.lineJoin = 'miter';
                 surfaces[f] = surface;
             });

             // Make the function that will draw our events
             drawEvent = function(event) {

                 if (event.type == "Evaluating") {
                     surfaces[event.name].fillStyle = "rgba(0, 0, 200, 1)";
                     surfaces[event.name].fillRect(event.coords[0]*7+1, event.coords[1]*7+1, 5, 5);  
                 } else if (event.type == "Realizing") {
                     //surfaces[event.name].fillStyle = "rgba(255, 255, 255, 1)";
                     //surfaces[event.name].fillRect(1, 1, 298, 298);
                     surfaces[event.name].strokeStyle = "rgba(0, 0, 0, 1)";
                     surfaces[event.name].strokeRect(event.coords[0]*7, event.coords[2]*7, event.coords[1]*7, event.coords[3]*7); 
                 } else if (event.type == "Discarding") {
                     //debug(event.coords);
                     for (var y = event.coords[2]; y < event.coords[2] + event.coords[3]; y++) {
                         for (var x = event.coords[0]; x < event.coords[0] + event.coords[1]; x++) {
                             surfaces[event.name].strokeStyle = "rgba(255, 255, 255, 1)";
                             surfaces[event.name].strokeRect(x*7+1, y*7+1, 5, 5);  
                         }
                     }
                     surfaces[event.name].strokeRect(event.coords[0]*7, event.coords[2]*7, event.coords[1]*7, event.coords[3]*7); 
                 }
             };

             var count = 0;

             loop = function() {
                 if (evals.length < count) return;
                 drawEvent(evals[count]);
                 count++;
                 t = setTimeout("loop()", 1);
             }

             loop();

         }
     });
 });