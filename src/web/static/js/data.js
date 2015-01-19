$(function(){
    $('#generate').click(function(){
        var project = $('#project').val();
        var activity = $('#activity').val();
        $.get(
            '/getdata',
            {projectid:project, activityid:activity},
            function(data){
                $('#results').html(data);
            }
        );
        return false;
    });
});
